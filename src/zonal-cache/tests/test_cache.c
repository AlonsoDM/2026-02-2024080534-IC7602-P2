#include <criterion/criterion.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache.h"
#include "config.h"
#include "http.h"

static TipoArchivo tipos[] = {{"html", 60}, {".png", 60}, {"json", 60}};

// Una URL con tipos html, png y json, LRU y el límite que pida la prueba
static UrlConfig url_con_limite(size_t limite)
{
    return (UrlConfig){.id = 7, .tamano_maximo = limite, .politica = "lru", .tipos = tipos, .num_tipos = 3};
}

static char directorio[] = "/tmp/cacheXXXXXX";

// El hilo que purga duerme una hora, así solo purga la prueba que llama cache_purgar
static void arrancar(void)
{
    cr_assert_not_null(mkdtemp(directorio));
    cr_assert_eq(cache_iniciar(directorio, 3600), 0);
}

// Cuántos archivos tiene la carpeta de la URL 7
static int archivos_en_disco(void)
{
    char ruta[64];
    snprintf(ruta, sizeof ruta, "%s/7", directorio);
    DIR *d = opendir(ruta);
    if(!d)
        return 0;
    int n = 0;
    for(struct dirent *e; (e = readdir(d));)
        n += e->d_name[0] != '.';
    closedir(d);
    return n;
}

// Arma una Peticion con el texto que mandaría un cliente
static void leer(const char *texto, Peticion *p)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    cr_assert_eq(write(par[1], texto, strlen(texto)), (ssize_t)strlen(texto));
    close(par[1]);
    Conexion conexion = {.fd = par[0]};
    cr_assert_eq(http_leer_peticion(&conexion, p), 0);
    conexion_cerrar(&conexion);
}

// Guarda lo que respondió el origen para un GET del host y la ruta
static void guardar(const UrlConfig *url, const char *metodo, const char *host, const char *ruta, int codigo,
                    const char *headers, const char *cuerpo)
{
    char texto[512];
    snprintf(texto, sizeof texto, "%s %s HTTP/1.1\r\nHost: %s\r\n\r\n", metodo, ruta, host);
    Peticion p;
    leer(texto, &p);
    Respuesta r = {.codigo = codigo, .headers = (char *)headers, .cuerpo = (char *)cuerpo,
                   .largo_cuerpo = strlen(cuerpo)};
    cache_guardar(url, &p, &r);
    http_liberar_peticion(&p);
}

// El cuerpo del hit o NULL si fue miss, y el que lo llama lo libera
static char *buscar(const UrlConfig *url, const char *host, const char *ruta, char **headers)
{
    char texto[512];
    snprintf(texto, sizeof texto, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", ruta, host);
    Peticion p;
    leer(texto, &p);
    Respuesta r = {0};
    int hit = cache_buscar(url, &p, &r) == 0;
    http_liberar_peticion(&p);
    if(!hit)
        return NULL;
    char *cuerpo = strndup(r.cuerpo, r.largo_cuerpo);
    if(headers)
        *headers = strdup(r.headers);
    http_liberar_respuesta(&r);
    return cuerpo;
}

Test(cache, hit_y_miss)
{
    arrancar();
    UrlConfig url = url_con_limite(1000);
    char *headers = NULL;
    cr_expect_null(buscar(&url, "www.marca.com", "/index.html", NULL));
    guardar(&url, "GET", "www.marca.com", "/index.html", 200, "Content-Type: text/html\r\n", "<p>hola</p>");
    char *cuerpo = buscar(&url, "WWW.Marca.com:80", "/index.html", &headers);
    cr_assert_not_null(cuerpo);
    cr_expect_str_eq(cuerpo, "<p>hola</p>");
    cr_expect_not_null(strstr(headers, "Content-Type: text/html\r\n"));
    cr_expect_not_null(strstr(headers, "X-Cache: HIT\r\n"));
    free(cuerpo);
    free(headers);
    cr_expect_null(buscar(&url, "img.marca.com", "/index.html", NULL));
    cr_expect_null(buscar(&url, "www.marca.com", "/index.html?v=2", NULL));
}

Test(cache, solo_guarda_lo_cacheable)
{
    arrancar();
    UrlConfig url = url_con_limite(1000);
    guardar(&url, "POST", "a.com", "/form.html", 200, "", "x");
    guardar(&url, "GET", "a.com", "/no-hay.html", 404, "", "x");
    guardar(&url, "GET", "a.com", "/perfil.html", 200, "Set-Cookie: s=1\r\n", "x");
    guardar(&url, "GET", "a.com", "/privado.html", 200, "Cache-Control: Private, max-age=60\r\n", "x");
    guardar(&url, "GET", "a.com", "/nunca.html", 200, "Cache-Control: no-store\r\n", "x");
    guardar(&url, "GET", "a.com", "/estilo.css", 200, "Content-Type: text/css\r\n", "x");
    guardar(&url, "GET", "a.com", "/api/texto", 200, "Content-Type: text/plain\r\n", "x");
    guardar(&url, "GET", "a.com", "/api/items", 200, "Content-Type: application/json; charset=utf-8\r\n", "[]");
    guardar(&url, "GET", "a.com", "/logo.PNG", 200, "Content-Type: image/png\r\n", "png");
    const char *no[] = {"/form.html", "/no-hay.html", "/perfil.html", "/privado.html", "/nunca.html", "/estilo.css",
                        "/api/texto"};
    for(size_t i = 0; i < sizeof no / sizeof no[0]; i++)
        cr_expect_null(buscar(&url, "a.com", no[i], NULL), "no debió guardar %s", no[i]);
    char *items = buscar(&url, "a.com", "/api/items", NULL);
    char *logo = buscar(&url, "a.com", "/logo.PNG", NULL);
    cr_expect_not_null(items, "sin extensión el tipo sale del Content-Type");
    cr_expect_not_null(logo, "la extensión no distingue mayúsculas y .png en la URL también vale");
    free(items);
    free(logo);
}

Test(cache, nunca_pasa_el_limite)
{
    arrancar();
    UrlConfig url = url_con_limite(10);
    const char *h = "Content-Type: text/html\r\n";
    guardar(&url, "GET", "a.com", "/a.html", 200, h, "123456");
    guardar(&url, "GET", "a.com", "/b.html", 200, h, "123456");
    guardar(&url, "GET", "a.com", "/grande.html", 200, h, "12345678901");
    cr_expect_null(buscar(&url, "a.com", "/a.html", NULL), "6 + 6 se pasa de 10 y sale a.html");
    cr_expect_null(buscar(&url, "a.com", "/grande.html", NULL), "11 nunca cabe en 10");
    char *b = buscar(&url, "a.com", "/b.html", NULL);
    cr_expect_not_null(b, "11 no sacó a nadie, porque igual no cabía");
    free(b);
    // Reemplazar b.html con 9 bytes cabe, porque sus 6 viejos dejan de contar
    guardar(&url, "GET", "a.com", "/b.html", 200, h, "123456789");
    b = buscar(&url, "a.com", "/b.html", NULL);
    cr_assert_not_null(b);
    cr_expect_str_eq(b, "123456789");
    free(b);
    cr_expect_eq(archivos_en_disco(), 1);
}

Test(cache, cada_tipo_vence_con_su_ttl)
{
    arrancar();
    TipoArchivo cortos[] = {{"html", 60}, {"png", 10}};
    UrlConfig url = {.id = 7, .tamano_maximo = 1000, .tipos = cortos, .num_tipos = 2};
    guardar(&url, "GET", "a.com", "/index.html", 200, "", "<p>hola</p>");
    guardar(&url, "GET", "a.com", "/logo.png", 200, "", "png");
    cache_adelantar_reloj(9);
    char *logo = buscar(&url, "a.com", "/logo.png", NULL);
    cr_expect_not_null(logo, "a los 9 segundos el png sigue vivo");
    free(logo);
    cache_adelantar_reloj(1);
    cr_expect_null(buscar(&url, "a.com", "/logo.png", NULL), "a los 10 segundos el png vence");
    char *pagina = buscar(&url, "a.com", "/index.html", NULL);
    cr_expect_not_null(pagina, "el html dura 60");
    free(pagina);
    cr_expect_eq(archivos_en_disco(), 1, "el png vencido se borró del disco al pedirlo");
    cache_adelantar_reloj(50);
    cr_expect_null(buscar(&url, "a.com", "/index.html", NULL));
}

Test(cache, purgar_borra_lo_que_nadie_pide)
{
    arrancar();
    UrlConfig url = url_con_limite(1000);
    const char *h = "Content-Type: text/html\r\n";
    guardar(&url, "GET", "a.com", "/a.html", 200, h, "a");
    guardar(&url, "GET", "a.com", "/b.html", 200, h, "b");
    guardar(&url, "GET", "a.com", "/c.html", 200, h, "c");
    cache_adelantar_reloj(30);
    guardar(&url, "GET", "a.com", "/d.html", 200, h, "d");
    cache_adelantar_reloj(30);
    cache_purgar();
    cr_expect_eq(archivos_en_disco(), 1, "a, b y c vencieron sin que nadie las pidiera");
    char *d = buscar(&url, "a.com", "/d.html", NULL);
    cr_expect_not_null(d, "d se guardó 30 segundos después y sigue viva");
    free(d);
}

Test(cache, lo_vencido_sale_antes_que_la_victima)
{
    arrancar();
    TipoArchivo cortos[] = {{"html", 60}, {"png", 10}};
    UrlConfig url = {.id = 7, .tamano_maximo = 12, .politica = "mru", .tipos = cortos, .num_tipos = 2};
    guardar(&url, "GET", "a.com", "/logo.png", 200, "", "123456");
    guardar(&url, "GET", "a.com", "/a.html", 200, "", "123456");
    cache_adelantar_reloj(10);
    guardar(&url, "GET", "a.com", "/b.html", 200, "", "123456");
    char *a = buscar(&url, "a.com", "/a.html", NULL);
    char *b = buscar(&url, "a.com", "/b.html", NULL);
    cr_expect_not_null(a, "MRU habría sacado a.html, pero el png vencido salió primero");
    cr_expect_not_null(b);
    free(a);
    free(b);
    cr_expect_eq(archivos_en_disco(), 2);
}

static const char *diez = "1234567890";

// Si la ruta está guardada, y cuenta como un uso
static int esta(const UrlConfig *url, const char *ruta)
{
    char *cuerpo = buscar(url, "a.com", ruta, NULL);
    int si = cuerpo != NULL;
    free(cuerpo);
    return si;
}

// Una URL de 30 bytes con esa política, llena con a, b y c de 10 en ese orden
static UrlConfig llena_con(const char *politica)
{
    arrancar();
    UrlConfig url = url_con_limite(30);
    url.politica = (char *)politica;
    guardar(&url, "GET", "a.com", "/a.html", 200, "", diez);
    guardar(&url, "GET", "a.com", "/b.html", 200, "", diez);
    guardar(&url, "GET", "a.com", "/c.html", 200, "", diez);
    return url;
}

// Guarda d y dice cuál de a, b y c salió para hacerle espacio
static char victima_al_guardar_d(const UrlConfig *url)
{
    guardar(url, "GET", "a.com", "/d.html", 200, "", diez);
    cr_assert_eq(archivos_en_disco(), 3, "sale exactamente una");
    cr_assert(esta(url, "/d.html"));
    const char *rutas[] = {"/a.html", "/b.html", "/c.html"};
    for(int i = 0; i < 3; i++)
        if(!esta(url, rutas[i]))
            return (char)('a' + i);
    return '?';
}

Test(cache, lru_saca_la_de_uso_mas_viejo)
{
    UrlConfig url = llena_con("lru");
    cr_assert(esta(&url, "/a.html"));
    cr_expect_eq(victima_al_guardar_d(&url), 'b');
}

Test(cache, mru_saca_la_de_uso_mas_reciente)
{
    UrlConfig url = llena_con("MRU");
    cr_assert(esta(&url, "/b.html"));
    cr_expect_eq(victima_al_guardar_d(&url), 'b');
}

Test(cache, fifo_saca_la_que_llego_primero)
{
    UrlConfig url = llena_con("fifo");
    cr_assert(esta(&url, "/a.html"));
    cr_assert(esta(&url, "/a.html"));
    cr_expect_eq(victima_al_guardar_d(&url), 'a', "aunque sea la más usada");
}

Test(cache, lfu_saca_la_menos_usada)
{
    UrlConfig url = llena_con("lfu");
    cr_assert(esta(&url, "/b.html"));
    cr_assert(esta(&url, "/a.html"));
    cr_expect_eq(victima_al_guardar_d(&url), 'c', "a y b tienen 2 usos, c solo 1");
}

Test(cache, lfu_desempata_por_lru)
{
    UrlConfig url = llena_con("lfu");
    cr_assert(esta(&url, "/a.html"));
    cr_assert(esta(&url, "/a.html"));
    cr_assert(esta(&url, "/c.html"));
    cr_assert(esta(&url, "/b.html"));
    cr_expect_eq(victima_al_guardar_d(&url), 'c', "b y c tienen 2 usos y c se usó antes");
}

Test(cache, random_saca_una_de_esa_url)
{
    UrlConfig url = llena_con("random");
    char v = victima_al_guardar_d(&url);
    cr_expect(v == 'a' || v == 'b' || v == 'c');
}

Test(cache, saca_varias_y_no_toca_otras_urls)
{
    UrlConfig url = llena_con("lru");
    UrlConfig otra = {.id = 9, .tamano_maximo = 30, .politica = "lru", .tipos = tipos, .num_tipos = 3};
    guardar(&otra, "GET", "b.com", "/x.html", 200, "", diez);
    guardar(&url, "GET", "a.com", "/grande.html", 200, "", "1234567890123456789012345");
    cr_expect_eq(archivos_en_disco(), 1, "25 bytes sacan a a, b y c");
    cr_expect(esta(&url, "/grande.html"));
    char *x = buscar(&otra, "b.com", "/x.html", NULL);
    cr_expect_not_null(x, "la otra URL tiene su propio límite");
    free(x);
}

// Guarda, busca y purga sin parar sobre siete rutas que no caben juntas
static void *trabajar(void *arg)
{
    const UrlConfig *url = arg;
    char ruta[32];
    for(int i = 0; i < 50; i++) {
        snprintf(ruta, sizeof ruta, "/%d.html", i % 7);
        guardar(url, "GET", "a.com", ruta, 200, "", diez);
        free(buscar(url, "a.com", ruta, NULL));
        cache_purgar();
    }
    return NULL;
}

Test(cache, varios_hilos_a_la_vez)
{
    UrlConfig url = llena_con("random");
    pthread_t hilos[4];
    for(int i = 0; i < 4; i++)
        cr_assert_eq(pthread_create(&hilos[i], NULL, trabajar, &url), 0);
    for(int i = 0; i < 4; i++)
        pthread_join(hilos[i], NULL);
    cr_expect_leq(archivos_en_disco(), 3, "30 bytes nunca pasan de tres archivos de 10");
}
