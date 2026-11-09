#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "http.h"

// Lee hasta el \r\n\r\n que cierra los headers, NULL si no llega o no cabe
static char *leer_cabecera(Conexion *conexion, char *crudo, size_t *leidos)
{
    *leidos = 0;
    crudo[0] = '\0';
    char *fin;
    // TCP es un flujo y un recv puede traer media línea
    while(!(fin = strstr(crudo, "\r\n\r\n"))) {
        if(*leidos == HTTP_MAX_CABECERA)
            return NULL;
        ssize_t n = conexion_leer(conexion, crudo + *leidos, HTTP_MAX_CABECERA - *leidos);
        if(n <= 0)
            return NULL;
        *leidos += (size_t)n;
        crudo[*leidos] = '\0';
    }
    return fin;
}

// Separa "GET /ruta HTTP/1.1" en sus tres partes
static int parsear_linea_peticion(char *linea, Peticion *p)
{
    char *resto;
    p->metodo = strtok_r(linea, " ", &resto);
    p->ruta = strtok_r(NULL, " ", &resto);
    char *version = strtok_r(NULL, " ", &resto);
    if(!p->metodo || !p->ruta || !version || strtok_r(NULL, " ", &resto))
        return -1;
    // Una ruta como http://otro.com/ es para un proxy de reenvío y no se acepta
    if(p->ruta[0] != '/')
        return -1;
    if(strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0)
        return -1;
    return 0;
}

// Separa un header en nombre y valor y lo agrega a la lista
static int parsear_header(char *linea, Peticion *p)
{
    char *dos_puntos = strchr(linea, ':');
    if(!dos_puntos || dos_puntos == linea)
        return -1;
    if(p->num_headers == HTTP_MAX_HEADERS)
        return -1;
    *dos_puntos = '\0';
    char *valor = dos_puntos + 1;
    while(*valor == ' ' || *valor == '\t')
        valor++;
    char *fin = valor + strlen(valor);
    while(fin > valor && (fin[-1] == ' ' || fin[-1] == '\t'))
        *--fin = '\0';
    p->headers[p->num_headers].nombre = linea;
    p->headers[p->num_headers].valor = valor;
    p->num_headers++;
    return 0;
}

// Content-Length a número, sin signo y sin pasar de HTTP_MAX_CUERPO
static int leer_largo(const char *valor, size_t *largo)
{
    // strtoul aceptaría "-5" o " 5"
    if(!isdigit((unsigned char)valor[0]))
        return -1;
    char *fin;
    errno = 0;
    unsigned long n = strtoul(valor, &fin, 10);
    if(errno || *fin != '\0' || n > HTTP_MAX_CUERPO)
        return -1;
    *largo = n;
    return 0;
}

// Junta lo que vino pegado a los headers con lo que falte por llegar
static int leer_cuerpo(Conexion *conexion, Peticion *p, const char *inicio, size_t ya_llegaron)
{
    if(p->largo_cuerpo == 0)
        return 0;
    p->cuerpo = malloc(p->largo_cuerpo);
    if(!p->cuerpo)
        return -1;
    size_t copiados = ya_llegaron < p->largo_cuerpo ? ya_llegaron : p->largo_cuerpo;
    memcpy(p->cuerpo, inicio, copiados);
    while(copiados < p->largo_cuerpo) {
        ssize_t n = conexion_leer(conexion, p->cuerpo + copiados, p->largo_cuerpo - copiados);
        if(n <= 0)
            return -1;
        copiados += (size_t)n;
    }
    return 0;
}

// Lee una petición completa, -1 si viene mal formada
int http_leer_peticion(Conexion *conexion, Peticion *p)
{
    // En cero para que liberar sirva aunque falle a medias
    memset(p, 0, sizeof *p);
    size_t leidos;
    char *fin = leer_cabecera(conexion, p->crudo, &leidos);
    if(!fin)
        return -1;
    *fin = '\0';
    char *inicio_cuerpo = fin + 4;
    size_t ya_llegaron = leidos - (size_t)(inicio_cuerpo - p->crudo);

    char *resto;
    char *linea = strtok_r(p->crudo, "\r\n", &resto);
    if(!linea || parsear_linea_peticion(linea, p) != 0)
        return -1;
    while((linea = strtok_r(NULL, "\r\n", &resto)))
        if(parsear_header(linea, p) != 0)
            return -1;

    // Todos los dominios de la zona llegan a la misma IP y el Host dice cuál es
    p->host = http_header(p, "Host");
    if(!p->host || !p->host[0])
        return -1;

    const char *largo = http_header(p, "Content-Length");
    if(largo && leer_largo(largo, &p->largo_cuerpo) != 0)
        return -1;
    return leer_cuerpo(conexion, p, inicio_cuerpo, ya_llegaron);
}

// Busca un header sin importar mayúsculas, NULL si no viene
const char *http_header(const Peticion *p, const char *nombre)
{
    for(size_t i = 0; i < p->num_headers; i++)
        if(strcasecmp(p->headers[i].nombre, nombre) == 0)
            return p->headers[i].valor;
    return NULL;
}

// Libera el cuerpo, lo único que se pidió aparte
void http_liberar_peticion(Peticion *p)
{
    free(p->cuerpo);
    p->cuerpo = NULL;
}

// Respuesta corta de texto para los errores
void http_respuesta_texto(Respuesta *r, int codigo, const char *texto)
{
    http_liberar_respuesta(r);
    r->codigo = codigo;
    // strdup porque liberar les hace free
    r->headers = strdup("Content-Type: text/plain; charset=utf-8\r\n");
    r->cuerpo = strdup(texto);
    r->largo_cuerpo = r->cuerpo ? strlen(texto) : 0;
}

// Frase de la línea de estado, opcional en HTTP/1.1
static const char *motivo(int codigo)
{
    switch(codigo) {
    case 200: return "OK";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 421: return "Misdirected Request";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "";
    }
}

// Una escritura puede mandar menos de lo pedido
static int enviar_todo(Conexion *conexion, const char *datos, size_t largo)
{
    while(largo > 0) {
        ssize_t n = conexion_escribir(conexion, datos, largo);
        if(n <= 0)
            return -1;
        datos += n;
        largo -= (size_t)n;
    }
    return 0;
}

// Línea de estado, headers, línea vacía y cuerpo
int http_enviar_respuesta(Conexion *conexion, const Respuesta *r)
{
    char cabecera[HTTP_MAX_CABECERA];
    int n = snprintf(cabecera, sizeof cabecera,
                     "HTTP/1.1 %d %s\r\n%sContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     r->codigo, motivo(r->codigo), r->headers ? r->headers : "", r->largo_cuerpo);
    if(n < 0 || (size_t)n >= sizeof cabecera)
        return -1;
    if(enviar_todo(conexion, cabecera, (size_t)n) != 0)
        return -1;
    return enviar_todo(conexion, r->cuerpo, r->largo_cuerpo);
}

// Libera y deja la respuesta en cero
void http_liberar_respuesta(Respuesta *r)
{
    free(r->headers);
    free(r->cuerpo);
    memset(r, 0, sizeof *r);
}

// Junta en un Buffer lo que libcurl va recibiendo, sin pasar de su límite
size_t http_acumular(char *pedazo, size_t tamano, size_t cuantos, void *destino)
{
    Buffer *b = destino;
    size_t n = tamano * cuantos;
    // Devolver otra cosa que n hace que libcurl corte
    if(n > b->limite - b->largo)
        return 0;
    char *mas = realloc(b->datos, b->largo + n + 1);
    if(!mas)
        return 0;
    memcpy(mas + b->largo, pedazo, n);
    b->datos = mas;
    b->largo += n;
    b->datos[b->largo] = '\0';
    return n;
}

// Une la dirección base con la ruta sin dejar // entre las dos, -1 si no cabe
int http_unir_url(char *destino, size_t tam, const char *base, const char *ruta)
{
    int largo = (int)strlen(base);
    while(largo > 0 && base[largo - 1] == '/')
        largo--;
    int n = snprintf(destino, tam, "%.*s%s", largo, base, ruta);
    return n < 0 || (size_t)n >= tam ? -1 : 0;
}
