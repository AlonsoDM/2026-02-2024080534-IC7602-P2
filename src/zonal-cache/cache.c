#include "cache.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

// CACHE_DIR más corto que una ruta, así siempre cabe con la carpeta y el número
#define MAX_DIRECTORIO 1024
#define MAX_RUTA 4096
#define MAX_EXTENSION 32

// Un recurso guardado, el cuerpo está en disco y lo demás en memoria
typedef struct {
    int url_id;
    unsigned long numero;
    char *clave;
    char *headers;
    size_t tamano;
    time_t expira;
    unsigned long ultimo_uso;
    unsigned long usos;
} Entrada;

// Búsqueda lineal, alcanza para cientos de recursos y si llegan a miles va una tabla hash
static pthread_mutex_t candado = PTHREAD_MUTEX_INITIALIZER;
static Entrada *entradas;
static size_t num_entradas;
static size_t capacidad;
static unsigned long ultimo_numero;
// Contador que sube con cada uso, así dos usos nunca empatan como con segundos
static unsigned long ultimo_tic;
static char directorio[MAX_DIRECTORIO];
static time_t desfase;
static int segundos_purga;

// Segundos de un reloj que nunca retrocede, más lo que adelanten las pruebas
static time_t ahora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + desfase;
}

// Solo para las pruebas, hace vencer entradas sin esperar
void cache_adelantar_reloj(time_t segundos)
{
    desfase += segundos;
}

// Saca las vencidas cada PURGE_INTERVAL_SECONDS, aunque nadie las pida
static void *purgar_siempre(void *arg)
{
    (void)arg;
    for(;;) {
        sleep((unsigned)segundos_purga);
        cache_purgar();
    }
    return NULL;
}

// Crea CACHE_DIR si no existe y arranca el hilo que purga, -1 si algo falla
int cache_iniciar(const char *dir, int segundos)
{
    // snprintf devuelve cuánto quiso escribir, si pasa del espacio la ruta no cupo
    if(snprintf(directorio, sizeof directorio, "%s", dir) >= (int)sizeof directorio) {
        log_error("CACHE_DIR \"%s\" es demasiado largo", dir);
        return -1;
    }
    if(mkdir(directorio, 0700) != 0 && errno != EEXIST) {
        log_error("No se pudo crear CACHE_DIR \"%s\"", dir);
        return -1;
    }
    if(access(directorio, W_OK) != 0) {
        log_error("No se puede escribir en CACHE_DIR \"%s\"", dir);
        return -1;
    }
    segundos_purga = segundos;
    // Una sola vez, sembrar de nuevo con la misma hora repetiría la víctima
    srand((unsigned)time(NULL));
    pthread_t hilo;
    if(pthread_create(&hilo, NULL, purgar_siempre, NULL) != 0) {
        log_error("No se pudo crear el hilo que purga la caché");
        return -1;
    }
    pthread_detach(hilo);
    return 0;
}

// La clave es el host en minúsculas sin puerto más la ruta con su query
static int armar_clave(const Peticion *p, char *clave, size_t tam)
{
    size_t largo_host = strcspn(p->host, ":");
    if(largo_host + strlen(p->ruta) + 1 > tam)
        return -1;
    for(size_t i = 0; i < largo_host; i++)
        clave[i] = (char)tolower((unsigned char)p->host[i]);
    strcpy(clave + largo_host, p->ruta);
    return 0;
}

// Dónde va el archivo de una entrada, CACHE_DIR/<id de la URL>/<número>
static void ruta_archivo(int url_id, unsigned long numero, char *ruta, size_t tam)
{
    snprintf(ruta, tam, "%s/%d/%lu", directorio, url_id, numero);
}

// La entrada con esa URL y esa clave, -1 si no está, con el candado ya tomado
static long buscar_entrada(int url_id, const char *clave)
{
    for(size_t i = 0; i < num_entradas; i++)
        if(entradas[i].url_id == url_id && strcmp(entradas[i].clave, clave) == 0)
            return (long)i;
    return -1;
}

// Bytes que ya ocupa una URL en disco, con el candado ya tomado
static size_t usado(int url_id)
{
    size_t total = 0;
    for(size_t i = 0; i < num_entradas; i++)
        if(entradas[i].url_id == url_id)
            total += entradas[i].tamano;
    return total;
}

// Borra el archivo de la entrada i y la saca del índice, con el candado ya tomado
static void quitar_entrada(size_t i)
{
    char ruta[MAX_RUTA];
    ruta_archivo(entradas[i].url_id, entradas[i].numero, ruta, sizeof ruta);
    unlink(ruta);
    free(entradas[i].clave);
    free(entradas[i].headers);
    // La última ocupa su lugar, así no queda hueco
    entradas[i] = entradas[--num_entradas];
}

// Quita todas las vencidas y dice cuántas, con el candado ya tomado
static size_t quitar_vencidas(void)
{
    size_t quitadas = 0;
    size_t i = 0;
    while(i < num_entradas) {
        // Al quitar, otra entrada cae en i y hay que revisarla
        if(ahora() >= entradas[i].expira) {
            quitar_entrada(i);
            quitadas++;
        } else {
            i++;
        }
    }
    return quitadas;
}

// 1 si a sale antes que b con esa política, LRU si no es ninguna de las otras
static int sale_antes(const char *politica, const Entrada *a, const Entrada *b)
{
    if(strcasecmp(politica, "fifo") == 0)
        return a->numero < b->numero;
    if(strcasecmp(politica, "mru") == 0)
        return a->ultimo_uso > b->ultimo_uso;
    // LFU desempata por LRU
    if(strcasecmp(politica, "lfu") == 0 && a->usos != b->usos)
        return a->usos < b->usos;
    return a->ultimo_uso < b->ultimo_uso;
}

// Una entrada cualquiera de la URL, -1 si no tiene ninguna
static long al_azar(int url_id)
{
    size_t n = 0;
    for(size_t i = 0; i < num_entradas; i++)
        if(entradas[i].url_id == url_id)
            n++;
    if(n == 0)
        return -1;
    // La k-ésima entrada de esa URL, contando desde 0
    size_t k = (size_t)rand() % n;
    for(size_t i = 0; i < num_entradas; i++) {
        if(entradas[i].url_id != url_id)
            continue;
        if(k == 0)
            return (long)i;
        k--;
    }
    return -1;
}

// La entrada de la URL que sale según su política, -1 si no tiene ninguna
static long escoger_victima(const UrlConfig *url)
{
    if(strcasecmp(url->politica, "random") == 0)
        return al_azar(url->id);
    long victima = -1;
    for(size_t i = 0; i < num_entradas; i++) {
        if(entradas[i].url_id != url->id)
            continue;
        if(victima < 0 || sale_antes(url->politica, &entradas[i], &entradas[victima]))
            victima = (long)i;
    }
    return victima;
}

// Copia en valor el header que se llama nombre, 0 si viene y -1 si no
static int buscar_header(const char *headers, const char *nombre, char *valor, size_t tam)
{
    size_t largo = strlen(nombre);
    for(const char *l = headers; l && *l;) {
        if(strncasecmp(l, nombre, largo) == 0 && l[largo] == ':') {
            const char *v = l + largo + 1;
            while(*v == ' ')
                v++;
            snprintf(valor, tam, "%.*s", (int)strcspn(v, "\r\n"), v);
            return 0;
        }
        l = strchr(l, '\n');
        if(l)
            l++;
    }
    return -1;
}

// Agranda el arreglo de entradas si está lleno, -1 si no hubo memoria
static int hacer_espacio(void)
{
    if(num_entradas < capacidad)
        return 0;
    size_t nueva = capacidad ? capacidad * 2 : 16;
    Entrada *mas = realloc(entradas, nueva * sizeof *mas);
    if(!mas)
        return -1;
    entradas = mas;
    capacidad = nueva;
    return 0;
}

// El tipo sale de la extensión de la ruta o, si no tiene, del Content-Type
static int tipo_de(const char *ruta, const char *headers, char *tipo, size_t tam)
{
    size_t largo = strcspn(ruta, "?#");
    const char *nombre = ruta;
    for(size_t i = 0; i < largo; i++)
        if(ruta[i] == '/')
            nombre = ruta + i + 1;
    const char *punto = NULL;
    for(const char *c = nombre; c < ruta + largo; c++)
        if(*c == '.')
            punto = c;
    if(punto && punto + 1 < ruta + largo) {
        int n = snprintf(tipo, tam, "%.*s", (int)(ruta + largo - punto - 1), punto + 1);
        return n > 0 && (size_t)n < tam ? 0 : -1;
    }
    char valor[128];
    if(buscar_header(headers, "Content-Type", valor, sizeof valor) != 0)
        return -1;
    char *barra = strchr(valor, '/');
    if(!barra)
        return -1;
    barra[1 + strcspn(barra + 1, "; ")] = '\0';
    // Subtipos que no se escriben igual que su extensión
    const char *alias[][2] = {{"javascript", "js"}, {"jpeg", "jpg"}, {"svg+xml", "svg"}, {"plain", "txt"}};
    const char *sub = barra + 1;
    for(size_t i = 0; i < sizeof alias / sizeof alias[0]; i++)
        if(strcasecmp(sub, alias[i][0]) == 0)
            sub = alias[i][1];
    int n = snprintf(tipo, tam, "%s", sub);
    return n > 0 && (size_t)n < tam ? 0 : -1;
}

// El TTL del tipo si la respuesta se puede guardar para esa URL, 0 si no
static int ttl_cacheable(const UrlConfig *url, const Peticion *p, const Respuesta *r)
{
    if(strcmp(p->metodo, "GET") != 0 || r->codigo != 200)
        return 0;
    char valor[256];
    if(buscar_header(r->headers, "Set-Cookie", valor, sizeof valor) == 0)
        return 0;
    if(buscar_header(r->headers, "Cache-Control", valor, sizeof valor) == 0) {
        for(char *c = valor; *c; c++)
            *c = (char)tolower((unsigned char)*c);
        if(strstr(valor, "private") || strstr(valor, "no-store"))
            return 0;
    }
    if(r->largo_cuerpo > url->tamano_maximo)
        return 0;
    char tipo[MAX_EXTENSION];
    if(tipo_de(p->ruta, r->headers, tipo, sizeof tipo) != 0)
        return 0;
    for(size_t i = 0; i < url->num_tipos; i++) {
        const char *e = url->tipos[i].extension;
        if(strcasecmp(e[0] == '.' ? e + 1 : e, tipo) == 0)
            return url->tipos[i].ttl_segundos;
    }
    return 0;
}

// Escribe el cuerpo en un temporal dentro de la carpeta de la URL
static int escribir_temporal(int url_id, const Respuesta *r, char *temporal, size_t tam)
{
    char carpeta[MAX_RUTA];
    snprintf(carpeta, sizeof carpeta, "%s/%d", directorio, url_id);
    if(mkdir(carpeta, 0700) != 0 && errno != EEXIST)
        return -1;
    if(snprintf(temporal, tam, "%s/.tmpXXXXXX", carpeta) >= (int)tam)
        return -1;
    int fd = mkstemp(temporal);
    if(fd < 0)
        return -1;
    size_t escritos = 0;
    while(escritos < r->largo_cuerpo) {
        ssize_t n = write(fd, r->cuerpo + escritos, r->largo_cuerpo - escritos);
        if(n <= 0) {
            close(fd);
            unlink(temporal);
            return -1;
        }
        escritos += (size_t)n;
    }
    return close(fd);
}

// Guarda la respuesta si es cacheable, sacando víctimas hasta que quepa en el límite de su URL
void cache_guardar(const UrlConfig *url, const Peticion *p, const Respuesta *r)
{
    char clave[MAX_RUTA], temporal[MAX_RUTA], final[MAX_RUTA];
    int ttl = ttl_cacheable(url, p, r);
    // Más grande que el límite nunca cabe, se sirve sin guardar y sin sacar a nadie
    if(ttl == 0 || r->largo_cuerpo > url->tamano_maximo || armar_clave(p, clave, sizeof clave) != 0)
        return;
    if(escribir_temporal(url->id, r, temporal, sizeof temporal) != 0) {
        log_error("No se pudo escribir en CACHE_DIR, %s se sirve sin guardar", clave);
        return;
    }
    char *copia_clave = strdup(clave);
    char *copia_headers = strdup(r->headers ? r->headers : "");
    int listo = 0;
    pthread_mutex_lock(&candado);
    // Lo vencido ya no sirve, sale antes de medir el espacio
    quitar_vencidas();
    // Si ya estaba, la nueva la reemplaza y su tamaño viejo deja de contar
    long i = buscar_entrada(url->id, clave);
    if(i >= 0)
        quitar_entrada((size_t)i);
    long victima;
    while(usado(url->id) + r->largo_cuerpo > url->tamano_maximo && (victima = escoger_victima(url)) >= 0)
        quitar_entrada((size_t)victima);
    unsigned long numero = ++ultimo_numero;
    ruta_archivo(url->id, numero, final, sizeof final);
    // rename es atómico, nadie lee un archivo a medias
    if(copia_clave && copia_headers && hacer_espacio() == 0 && rename(temporal, final) == 0) {
        entradas[num_entradas++] = (Entrada){.url_id = url->id, .numero = numero, .clave = copia_clave,
                                             .headers = copia_headers, .tamano = r->largo_cuerpo,
                                             .expira = ahora() + ttl, .ultimo_uso = ++ultimo_tic, .usos = 1};
        copia_clave = NULL;
        copia_headers = NULL;
        listo = 1;
    }
    pthread_mutex_unlock(&candado);
    if(!listo)
        unlink(temporal);
    free(copia_clave);
    free(copia_headers);
}

// Lee un archivo entero a memoria, NULL si ya no existe
static char *leer_archivo(const char *ruta, size_t *largo)
{
    int fd = open(ruta, O_RDONLY);
    struct stat info;
    if(fd < 0)
        return NULL;
    char *datos = NULL;
    if(fstat(fd, &info) == 0 && (datos = malloc((size_t)info.st_size + 1))) {
        size_t leidos = 0;
        while(leidos < (size_t)info.st_size) {
            ssize_t n = read(fd, datos + leidos, (size_t)info.st_size - leidos);
            if(n <= 0)
                break;
            leidos += (size_t)n;
        }
        *largo = leidos;
    }
    close(fd);
    return datos;
}

// Deja el recurso guardado en salida con X-Cache HIT, -1 si no está
int cache_buscar(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    char clave[MAX_RUTA], ruta[MAX_RUTA];
    if(strcmp(p->metodo, "GET") != 0 || armar_clave(p, clave, sizeof clave) != 0)
        return -1;
    pthread_mutex_lock(&candado);
    long i = buscar_entrada(url->id, clave);
    // Vencida se borra y cuenta como MISS
    if(i >= 0 && ahora() >= entradas[i].expira) {
        quitar_entrada((size_t)i);
        i = -1;
    }
    char *headers = NULL;
    if(i >= 0) {
        ruta_archivo(url->id, entradas[i].numero, ruta, sizeof ruta);
        headers = strdup(entradas[i].headers);
        entradas[i].ultimo_uso = ++ultimo_tic;
        entradas[i].usos++;
    }
    pthread_mutex_unlock(&candado);
    if(!headers)
        return -1;
    size_t largo = 0;
    char *cuerpo = leer_archivo(ruta, &largo);
    size_t n = strlen(headers);
    char *con_hit = cuerpo ? realloc(headers, n + sizeof "X-Cache: HIT\r\n") : NULL;
    if(!con_hit) {
        free(cuerpo);
        free(headers);
        return -1;
    }
    strcpy(con_hit + n, "X-Cache: HIT\r\n");
    salida->codigo = 200;
    salida->headers = con_hit;
    salida->cuerpo = cuerpo;
    salida->largo_cuerpo = largo;
    return 0;
}

// Saca del disco y del índice todas las entradas vencidas
void cache_purgar(void)
{
    pthread_mutex_lock(&candado);
    size_t quitadas = quitar_vencidas();
    pthread_mutex_unlock(&candado);
    if(quitadas > 0)
        log_info("Se purgaron %zu entradas vencidas", quitadas);
}
