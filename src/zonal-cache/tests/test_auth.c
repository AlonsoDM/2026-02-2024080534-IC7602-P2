#include <criterion/criterion.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "auth.h"
#include "config.h"
#include "fake_server.h"
#include "servidor.h"

// Un puerto por prueba de servidor porque Criterion puede correrlas en paralelo
#define PUERTO_SIN_LLAVE 9881
#define PUERTO_LLAVE_MALA 9882
#define PUERTO_SESION 9883

// Arranca el API falso y deja a auth apuntando a él
static void iniciar_api(fake_server *api)
{
    cr_assert_eq(fake_server_start(api), 0);
    static char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d", api->port);
    static ConfigCache ajustes;
    ajustes = (ConfigCache){
        .api_url = url, .api_key = "llave-de-prueba", .ui_login_url = "https://ui.example.com/login"};
    curl_global_init(CURL_GLOBAL_DEFAULT);
    auth_iniciar(&ajustes);
}

// Una petición ya leída con un solo header además de Host
static Peticion peticion(const char *nombre, const char *valor)
{
    Peticion p = {.metodo = "GET", .ruta = "/", .host = "seguro.marca.com"};
    p.headers[p.num_headers++] = (Header){"Host", "seguro.marca.com"};
    if(nombre)
        p.headers[p.num_headers++] = (Header){nombre, valor};
    return p;
}

static const UrlConfig con_llave = {.id = 7, .auth = "api_key"};

Test(auth, sin_seguridad_no_pregunta)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    iniciar_api(&api);
    UrlConfig libre = {.id = 7, .auth = "none"};
    Peticion p = peticion(NULL, NULL);
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&libre, &p, &r), 0);
    cr_expect_eq(r.codigo, 0);
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 0);
}

Test(auth, llave_valida_pasa)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    Peticion p = peticion("X-Api-Key", "dev-java-key");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_llave, &p, &r), 0);
    cr_expect_eq(r.codigo, 0);

    char enviada[FAKE_SERVER_REQUEST_SIZE];
    fake_server_copy_last_request(&api, enviada, sizeof enviada);
    fake_server_stop(&api);
    cr_expect_not_null(strstr(enviada, "POST /api/cache/api-keys/verify HTTP/1.1\r\n"));
    // La llave de componente va en el header y la del cliente en el cuerpo
    cr_expect_not_null(strstr(enviada, "x-api-key: llave-de-prueba\r\n"));
    cr_expect_not_null(strstr(enviada, "Content-Type: application/json\r\n"));
    cr_expect_not_null(strstr(enviada, "{\"url_id\":7,\"key\":\"dev-java-key\"}"));
    http_liberar_respuesta(&r);
}

Test(auth, llave_invalida_da_401)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    iniciar_api(&api);
    Peticion p = peticion("x-api-key", "inventada");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_llave, &p, &r), -1);
    cr_expect_eq(r.codigo, 401);
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 1);
    http_liberar_respuesta(&r);
}

Test(auth, sin_llave_da_401_sin_preguntar)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    Peticion p = peticion(NULL, NULL);
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_llave, &p, &r), -1);
    cr_expect_eq(r.codigo, 401);

    Peticion vacia = peticion("x-api-key", "");
    Respuesta r2 = {0};
    cr_expect_eq(auth_verificar(&con_llave, &vacia, &r2), -1);
    cr_expect_eq(r2.codigo, 401);
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 0);
    http_liberar_respuesta(&r);
    http_liberar_respuesta(&r2);
}

Test(auth, llave_con_comillas_no_rompe_el_json)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    iniciar_api(&api);
    Peticion p = peticion("x-api-key", "a\",\"valid\":true");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_llave, &p, &r), -1);
    char enviada[FAKE_SERVER_REQUEST_SIZE];
    fake_server_copy_last_request(&api, enviada, sizeof enviada);
    fake_server_stop(&api);
    cr_expect_not_null(strstr(enviada, "{\"url_id\":7,\"key\":\"a\\\",\\\"valid\\\":true\"}"));
    http_liberar_respuesta(&r);
}

Test(auth, si_el_api_falla_no_deja_pasar)
{
    const char *cuerpos[] = {NULL, "no es json", "{\"valid\": \"true\"}"};
    int codigos[] = {500, 200, 200};
    for(size_t i = 0; i < 3; i++) {
        fake_server api = {.speak_http = 1, .response_status = codigos[i], .response_body = cuerpos[i]};
        iniciar_api(&api);
        Peticion p = peticion("x-api-key", "dev-java-key");
        Respuesta r = {0};
        cr_expect_eq(auth_verificar(&con_llave, &p, &r), -1, "caso %zu", i);
        cr_expect_eq(r.codigo, 503, "caso %zu", i);
        fake_server_stop(&api);
        http_liberar_respuesta(&r);
    }
}

// Usuario y contraseña

static const UrlConfig con_usuario = {.id = 4, .auth = "user_password"};

static Peticion peticion_privada(const char *ruta, const char *cookie)
{
    Peticion p = {.metodo = "GET", .ruta = ruta, .host = "privado.marca.com"};
    p.headers[p.num_headers++] = (Header){"Host", "privado.marca.com"};
    if(cookie)
        p.headers[p.num_headers++] = (Header){"Cookie", cookie};
    return p;
}

// La línea "Nombre: valor" de la respuesta, sin el \r\n, o "" si no viene
static const char *header_de(const Respuesta *r, const char *nombre, char *destino, size_t tam)
{
    destino[0] = '\0';
    const char *inicio = r->headers ? strstr(r->headers, nombre) : NULL;
    if(inicio) {
        size_t largo = strcspn(inicio, "\r");
        snprintf(destino, tam, "%.*s", (int)largo, inicio);
    }
    return destino;
}

Test(auth, sin_cookie_da_302_al_login_con_callback)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    Peticion p = peticion_privada("/docs/a b.html?x=1&y=2", NULL);
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &p, &r), -1);
    cr_expect_eq(r.codigo, 302);
    char linea[1024];
    // El callback va codificado una vez y la ruta dentro de él, dos
    cr_expect_str_eq(header_de(&r, "Location:", linea, sizeof linea),
                     "Location: https://ui.example.com/login?url=4&callback="
                     "http%3A%2F%2Fprivado.marca.com%2F__auth%2Fcallback%3Fnext%3D"
                     "%252Fdocs%252Fa%2520b.html%253Fx%253D1%2526y%253D2");
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 0);
    http_liberar_respuesta(&r);
}

Test(auth, por_https_el_callback_es_https)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    Peticion p = peticion_privada("/", NULL);
    p.tls = true;
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &p, &r), -1);
    char linea[1024];
    cr_expect_not_null(strstr(header_de(&r, "Location:", linea, sizeof linea), "callback=https%3A%2F%2F"));
    fake_server_stop(&api);
    http_liberar_respuesta(&r);
}

Test(auth, cookie_valida_pasa)
{
    fake_server api = {.speak_http = 1, .response_status = 200,
                       .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    // Entre otras cookies, como las manda un navegador
    Peticion p = peticion_privada("/", "tema=oscuro; zc_session=abc123; otra=1");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &p, &r), 0);
    cr_expect_eq(r.codigo, 0);
    char enviada[FAKE_SERVER_REQUEST_SIZE];
    fake_server_copy_last_request(&api, enviada, sizeof enviada);
    fake_server_stop(&api);
    cr_expect_not_null(strstr(enviada, "POST /api/cache/sessions/verify HTTP/1.1\r\n"));
    cr_expect_not_null(strstr(enviada, "x-api-key: llave-de-prueba\r\n"));
    cr_expect_not_null(strstr(enviada, "{\"url_id\":4,\"session\":\"abc123\"}"));
}

Test(auth, cookie_vencida_o_mal_formada_vuelve_al_login)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    iniciar_api(&api);
    Peticion vencida = peticion_privada("/", "zc_session=abc123");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &vencida, &r), -1);
    cr_expect_eq(r.codigo, 302);
    cr_expect_eq(fake_server_connection_count(&api), 1);

    // Con caracteres raros ni se pregunta
    Peticion rara = peticion_privada("/", "zc_session=a\"b");
    Respuesta r2 = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &rara, &r2), -1);
    cr_expect_eq(r2.codigo, 302);
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 1);
    http_liberar_respuesta(&r);
    http_liberar_respuesta(&r2);
}

Test(auth, si_el_api_falla_la_sesion_no_pasa)
{
    fake_server api = {.speak_http = 1, .response_status = 500};
    iniciar_api(&api);
    Peticion p = peticion_privada("/", "zc_session=abc123");
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &p, &r), -1);
    cr_expect_eq(r.codigo, 503);
    fake_server_stop(&api);
    http_liberar_respuesta(&r);
}

Test(auth, callback_deja_la_cookie_y_vuelve_a_la_ruta)
{
    fake_server api = {.speak_http = 1, .response_status = 200,
                       .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    // Como lo arma la UI: el callback que recibió más &session=
    Peticion p = peticion_privada("/__auth/callback?next=%2Fdocs%3Fx%3D1&session=abc123", NULL);
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &p, &r), -1);
    cr_expect_eq(r.codigo, 302);
    char linea[512];
    cr_expect_str_eq(header_de(&r, "Location:", linea, sizeof linea), "Location: /docs?x=1");
    cr_expect_str_eq(header_de(&r, "Set-Cookie:", linea, sizeof linea),
                     "Set-Cookie: zc_session=abc123; Path=/; HttpOnly; SameSite=Lax");
    char enviada[FAKE_SERVER_REQUEST_SIZE];
    fake_server_copy_last_request(&api, enviada, sizeof enviada);
    fake_server_stop(&api);
    cr_expect_not_null(strstr(enviada, "{\"url_id\":4,\"session\":\"abc123\"}"));
    http_liberar_respuesta(&r);
}

Test(auth, callback_por_https_marca_la_cookie_secure)
{
    fake_server api = {.speak_http = 1, .response_status = 200,
                       .response_body = "{\"valid\": true}"};
    iniciar_api(&api);
    Peticion p = peticion_privada("/__auth/callback?session=abc123&next=%2F", NULL);
    p.tls = true;
    Respuesta r = {0};
    auth_verificar(&con_usuario, &p, &r);
    char linea[512];
    cr_expect_str_eq(header_de(&r, "Set-Cookie:", linea, sizeof linea),
                     "Set-Cookie: zc_session=abc123; Path=/; HttpOnly; SameSite=Lax; Secure");
    fake_server_stop(&api);
    http_liberar_respuesta(&r);
}

Test(auth, callback_no_redirige_a_otro_sitio)
{
    const char *rutas[] = {
        "/__auth/callback?session=abc123&next=%2F%2Fmalo.com",
        "/__auth/callback?session=abc123&next=https%3A%2F%2Fmalo.com",
        "/__auth/callback?session=abc123&next=%2F%5Cmalo.com",
        "/__auth/callback?session=abc123&next=%2Fa%0D%0ASet-Cookie%3A%20x%3D1",
        "/__auth/callback?session=abc123",
    };
    for(size_t i = 0; i < sizeof rutas / sizeof rutas[0]; i++) {
        fake_server api = {.speak_http = 1, .response_status = 200,
                           .response_body = "{\"valid\": true}"};
        iniciar_api(&api);
        Peticion p = peticion_privada(rutas[i], NULL);
        Respuesta r = {0};
        auth_verificar(&con_usuario, &p, &r);
        char linea[512];
        cr_expect_str_eq(header_de(&r, "Location:", linea, sizeof linea), "Location: /", "caso %zu", i);
        fake_server_stop(&api);
        http_liberar_respuesta(&r);
    }
}

Test(auth, callback_con_sesion_mala)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    iniciar_api(&api);
    Peticion invalida = peticion_privada("/__auth/callback?session=abc123&next=%2F", NULL);
    Respuesta r = {0};
    cr_expect_eq(auth_verificar(&con_usuario, &invalida, &r), -1);
    cr_expect_eq(r.codigo, 401);
    cr_expect_null(strstr(r.headers, "Set-Cookie"));

    // Sin sesión, o con una que metería atributos en la cookie, ni se pregunta
    const char *malas[] = {"/__auth/callback", "/__auth/callback?next=%2F",
                           "/__auth/callback?session=abc%3B%20Domain%3Dmalo.com"};
    for(size_t i = 0; i < 3; i++) {
        Peticion p = peticion_privada(malas[i], NULL);
        Respuesta r2 = {0};
        cr_expect_eq(auth_verificar(&con_usuario, &p, &r2), -1);
        cr_expect_eq(r2.codigo, 400, "caso %zu", i);
        http_liberar_respuesta(&r2);
    }
    fake_server_stop(&api);
    cr_expect_eq(api.connection_count, 1);
    http_liberar_respuesta(&r);
}

// De punta a punta: el 401 sale antes de llegar al origen

static void *correr_servidor(void *arg)
{
    int servidor = (int)(intptr_t)arg;
    servidor_atender(servidor, -1);
    close(servidor);
    return NULL;
}

// Instala una URL con api_key y otra con user_password cuyo origen es el servidor falso y arranca la caché
static void arrancar_cache(int puerto, const fake_server *origen)
{
    char json[1024];
    snprintf(json, sizeof json,
             "{\"urls\": [{\"id\": 7, \"pattern\": \"seguro.marca.com\", \"origin\": \"http://127.0.0.1:%d\", "
             "\"tls_enabled\": false, \"cache_size_bytes\": 1000, \"replacement_policy\": \"lru\", "
             "\"auth\": \"api_key\", \"file_types\": [{\"extension\": \"html\", \"ttl_seconds\": 60}]}, "
             "{\"id\": 4, \"pattern\": \"privado.marca.com\", \"origin\": \"http://127.0.0.1:%d\", "
             "\"tls_enabled\": false, \"cache_size_bytes\": 1000, \"replacement_policy\": \"lru\", "
             "\"auth\": \"user_password\", \"file_types\": []}]}",
             origen->port, origen->port);
    Configuracion *c = config_parsear(json);
    cr_assert_not_null(c);
    config_instalar(c);
    int servidor = servidor_abrir(puerto);
    cr_assert_geq(servidor, 0);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, correr_servidor, (void *)(intptr_t)servidor), 0);
    pthread_detach(hilo);
}

// Manda la petición reintentando mientras arranca y devuelve la línea de estado
static void pedir(int puerto, const char *texto, char *linea, size_t tam)
{
    struct sockaddr_in direccion = {.sin_family = AF_INET, .sin_port = htons((uint16_t)puerto)};
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int fd = -1;
    for(int intento = 0; intento < 50 && fd < 0; intento++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if(connect(fd, (struct sockaddr *)&direccion, sizeof direccion) != 0) {
            close(fd);
            fd = -1;
            usleep(100000);
        }
    }
    cr_assert_geq(fd, 0, "la caché no arrancó en el puerto %d", puerto);
    struct timeval espera = {.tv_sec = 3};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera);
    cr_assert_eq(send(fd, texto, strlen(texto), 0), (ssize_t)strlen(texto));
    ssize_t n = recv(fd, linea, tam - 1, 0);
    close(fd);
    linea[n > 0 ? n : 0] = '\0';
    linea[strcspn(linea, "\r")] = '\0';
}

Test(auth, sin_llave_no_toca_el_origen)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": true}"};
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "hola"};
    iniciar_api(&api);
    cr_assert_eq(fake_server_start(&origen), 0);
    arrancar_cache(PUERTO_SIN_LLAVE, &origen);
    char linea[256];
    pedir(PUERTO_SIN_LLAVE, "GET / HTTP/1.1\r\nHost: seguro.marca.com\r\n\r\n", linea, sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 401 Unauthorized");
    cr_expect_eq(fake_server_connection_count(&api), 0);
    cr_expect_eq(fake_server_connection_count(&origen), 0);
}

Test(auth, llave_mala_no_toca_el_origen_y_la_buena_si)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "hola"};
    iniciar_api(&api);
    cr_assert_eq(fake_server_start(&origen), 0);
    arrancar_cache(PUERTO_LLAVE_MALA, &origen);
    char linea[256];
    pedir(PUERTO_LLAVE_MALA, "GET / HTTP/1.1\r\nHost: seguro.marca.com\r\nx-api-key: inventada\r\n\r\n", linea,
          sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 401 Unauthorized");
    cr_expect_eq(fake_server_connection_count(&origen), 0);

    // El mismo camino con una llave que el API acepta sí llega al origen
    api.response_body = "{\"valid\": true}";
    pedir(PUERTO_LLAVE_MALA, "GET / HTTP/1.1\r\nHost: seguro.marca.com\r\nx-api-key: dev-java-key\r\n\r\n", linea,
          sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 200 OK");
    cr_expect_eq(fake_server_connection_count(&origen), 1);
}

Test(auth, sin_sesion_no_toca_el_origen_y_con_sesion_si)
{
    fake_server api = {.speak_http = 1, .response_status = 200, .response_body = "{\"valid\": false}"};
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "hola"};
    iniciar_api(&api);
    cr_assert_eq(fake_server_start(&origen), 0);
    arrancar_cache(PUERTO_SESION, &origen);
    char linea[256];
    pedir(PUERTO_SESION, "GET / HTTP/1.1\r\nHost: privado.marca.com\r\n\r\n", linea, sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 302 Found");
    pedir(PUERTO_SESION, "GET / HTTP/1.1\r\nHost: privado.marca.com\r\nCookie: zc_session=abc123\r\n\r\n", linea,
          sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 302 Found");
    cr_expect_eq(fake_server_connection_count(&origen), 0);

    api.response_body = "{\"valid\": true}";
    pedir(PUERTO_SESION, "GET / HTTP/1.1\r\nHost: privado.marca.com\r\nCookie: zc_session=abc123\r\n\r\n", linea,
          sizeof linea);
    cr_expect_str_eq(linea, "HTTP/1.1 200 OK");
    cr_expect_eq(fake_server_connection_count(&origen), 1);
}
