#include <criterion/criterion.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "conexion.h"
#include "fake_server.h"
#include "servidor.h"
#include "tls.h"

#define DIA (24 * 60 * 60)
#define REGLA(patron, tls) "{\"id\":1,\"pattern\":\"" patron "\",\"tls_enabled\":" tls \
    ",\"origin\":\"http://origen\",\"cache_size_bytes\":1000," \
    "\"replacement_policy\":\"lru\",\"auth\":\"none\",\"file_types\":[]}"

static char directorio[] = "/tmp/zonal-tls-XXXXXX";
static char ruta_cert[128], ruta_llave[128];
static X509 *ca;
static EVP_PKEY *llave;
static time_t ahora;
static int listeners[2] = {-1, -1};
static int puerto_http, puerto_https;
static pthread_t hilo_servidor;

static void extension(X509 *cert, int nid, const char *valor)
{
    X509V3_CTX contexto;
    X509V3_set_ctx(&contexto, cert, cert, NULL, NULL, 0);
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &contexto, nid, valor);
    cr_assert_not_null(ext);
    cr_assert_eq(X509_add_ext(cert, ext, -1), 1);
    X509_EXTENSION_free(ext);
}

static void guardar_ca(void)
{
    cr_assert_gt(X509_sign(ca, llave, EVP_sha256()), 0);
    FILE *archivo = fopen(ruta_cert, "w");
    cr_assert_not_null(archivo);
    cr_assert_eq(PEM_write_X509(archivo, ca), 1);
    cr_assert_eq(fclose(archivo), 0);
    archivo = fopen(ruta_llave, "w");
    cr_assert_not_null(archivo);
    cr_assert_eq(PEM_write_PrivateKey(archivo, llave, NULL, NULL, 0, NULL, NULL), 1);
    cr_assert_eq(fclose(archivo), 0);
}

static void preparar(void)
{
    cr_assert_neq(signal(SIGPIPE, SIG_IGN), SIG_ERR);
    cr_assert_not_null(mkdtemp(directorio));
    snprintf(ruta_cert, sizeof ruta_cert, "%s/ca.pem", directorio);
    snprintf(ruta_llave, sizeof ruta_llave, "%s/ca-key.pem", directorio);
    ahora = time(NULL);
    llave = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
    ca = X509_new();
    cr_assert_not_null(llave);
    cr_assert_not_null(ca);
    cr_assert_eq(X509_set_version(ca, 2), 1);
    cr_assert_eq(ASN1_INTEGER_set(X509_get_serialNumber(ca), 1), 1);
    cr_assert_not_null(ASN1_TIME_set(X509_getm_notBefore(ca), ahora - 60));
    cr_assert_not_null(ASN1_TIME_adj(X509_getm_notAfter(ca), ahora, 365, 0));
    cr_assert_eq(X509_set_pubkey(ca, llave), 1);
    cr_assert_eq(X509_NAME_add_entry_by_txt(X509_get_subject_name(ca), "CN", MBSTRING_ASC,
                                           (const unsigned char *)"CA de prueba", -1, -1, 0), 1);
    cr_assert_eq(X509_set_issuer_name(ca, X509_get_subject_name(ca)), 1);
    extension(ca, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    extension(ca, NID_key_usage, "critical,keyCertSign,cRLSign");
    extension(ca, NID_subject_key_identifier, "hash");
    guardar_ca();
}

static void terminar(void)
{
    if(listeners[0] >= 0) {
        shutdown(listeners[0], SHUT_RDWR);
        shutdown(listeners[1], SHUT_RDWR);
        pthread_join(hilo_servidor, NULL);
        close(listeners[0]);
        close(listeners[1]);
    }
    tls_liberar();
    X509_free(ca);
    EVP_PKEY_free(llave);
    unlink(ruta_cert);
    unlink(ruta_llave);
    rmdir(directorio);
}

TestSuite(tls, .init = preparar, .fini = terminar, .timeout = 15);

static void instalar(const char *texto)
{
    Configuracion *config = config_parsear(texto);
    cr_assert_not_null(config);
    config_instalar(config);
}

static void iniciar(void)
{
    cr_assert_eq(tls_iniciar(ruta_cert, ruta_llave), 0);
    instalar("{\"urls\":[" REGLA("*.marca.com", "true") ","
             REGLA("api.marca.com", "true") "," REGLA("sin-tls.com", "false") "]}");
}

static void verificar_certificado(SSL_CTX *ctx, const char *patron)
{
    X509 *cert = SSL_CTX_get0_certificate(ctx);
    cr_assert_not_null(cert);
    cr_expect_eq(X509_verify(cert, llave), 1);
    cr_expect_eq(X509_check_ca(cert), 0);
    cr_expect(X509_get_key_usage(cert) & KU_DIGITAL_SIGNATURE);
    cr_expect(X509_get_extended_key_usage(cert) & XKU_SSL_SERVER);
    cr_expect_eq(X509_get_signature_nid(cert), NID_sha256WithRSAEncryption);
    EVP_PKEY *publica = X509_get0_pubkey(cert);
    cr_expect_eq(EVP_PKEY_get_bits(publica), 2048);
    cr_expect_eq(SSL_CTX_check_private_key(ctx), 1);
    cr_expect_geq(X509_get_ext_by_NID(cert, NID_subject_key_identifier, -1), 0);
    cr_expect_geq(X509_get_ext_by_NID(cert, NID_authority_key_identifier, -1), 0);
    GENERAL_NAMES *nombres = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
    cr_assert_not_null(nombres);
    cr_assert_eq(sk_GENERAL_NAME_num(nombres), 1);
    GENERAL_NAME *nombre = sk_GENERAL_NAME_value(nombres, 0);
    cr_assert_eq(nombre->type, GEN_DNS);
    cr_assert_eq(ASN1_STRING_length(nombre->d.dNSName), (int)strlen(patron));
    cr_expect_eq(memcmp(ASN1_STRING_get0_data(nombre->d.dNSName), patron, strlen(patron)), 0);
    GENERAL_NAMES_free(nombres);
    cr_expect_leq(ASN1_TIME_compare(X509_get0_notAfter(cert), X509_get0_notAfter(ca)), 0);
}

Test(tls, emite_certificados_exactos_y_wildcard)
{
    iniciar();
    SSL_CTX *exacto = tls_ensure_cert("API.MARCA.COM", ahora);
    SSL_CTX *wildcard = tls_ensure_cert("*.marca.com", ahora);
    cr_assert_not_null(exacto);
    cr_assert_not_null(wildcard);
    verificar_certificado(exacto, "api.marca.com");
    verificar_certificado(wildcard, "*.marca.com");
    ASN1_TIME *fin = ASN1_TIME_adj(NULL, ahora, 30, 0);
    cr_assert_not_null(fin);
    cr_expect_eq(ASN1_TIME_compare(X509_get0_notAfter(SSL_CTX_get0_certificate(exacto)), fin), 0);
    ASN1_TIME_free(fin);
    SSL_CTX_free(exacto);
    SSL_CTX_free(wildcard);
    cr_expect_null(tls_ensure_cert("sin-tls.com", ahora));
    cr_expect_null(tls_ensure_cert("desconocido.com", ahora));
    cr_expect_null(tls_ensure_cert("*.com,DNS:otro.com", ahora));
}

Test(tls, limita_vigencia_por_la_ca_y_rechaza_ca_vencida)
{
    cr_assert_not_null(ASN1_TIME_adj(X509_getm_notAfter(ca), ahora, 2, 0));
    guardar_ca();
    iniciar();
    SSL_CTX *ctx = tls_ensure_cert("api.marca.com", ahora);
    cr_assert_not_null(ctx);
    cr_expect_eq(ASN1_TIME_compare(X509_get0_notAfter(SSL_CTX_get0_certificate(ctx)),
                                  X509_get0_notAfter(ca)), 0);
    SSL_CTX_free(ctx);
    cr_expect_null(tls_ensure_cert("api.marca.com", ahora + 3 * DIA));
}

Test(tls, renueva_con_referencias_abiertas)
{
    iniciar();
    SSL_CTX *viejo = tls_ensure_cert("api.marca.com", ahora);
    cr_assert_not_null(viejo);
    SSL *abierto = SSL_new(viejo);
    cr_assert_not_null(abierto);
    SSL_CTX *mismo = tls_ensure_cert("api.marca.com", ahora + 28 * DIA);
    cr_assert_eq(mismo, viejo);
    SSL_CTX_free(mismo);
    SSL_CTX *nuevo = tls_ensure_cert("api.marca.com", ahora + 29 * DIA);
    cr_assert_not_null(nuevo);
    cr_expect_neq(nuevo, viejo);
    SSL_CTX_free(viejo);
    cr_expect_eq(SSL_get_SSL_CTX(abierto), viejo);
    cr_expect_eq(SSL_check_private_key(abierto), 1);
    verificar_certificado(nuevo, "api.marca.com");
    SSL_free(abierto);
    SSL_CTX_free(nuevo);
}

Test(tls, retira_patrones_sin_liberar_conexiones_abiertas)
{
    iniciar();
    SSL_CTX *viejo = tls_ensure_cert("*.marca.com", ahora);
    cr_assert_not_null(viejo);
    SSL *abierto = SSL_new(viejo);
    cr_assert_not_null(abierto);
    SSL_CTX_free(viejo);
    instalar("{\"urls\":[" REGLA("api.marca.com", "true") "," REGLA("*.marca.com", "false") "]}");
    cr_expect_null(tls_ensure_cert("*.marca.com", ahora));
    SSL_CTX *nuevo = tls_ensure_cert("api.marca.com", ahora);
    cr_assert_not_null(nuevo); // La nueva emisión retira la wildcard.
    cr_expect_eq(SSL_check_private_key(abierto), 1);
    instalar("{\"urls\":[" REGLA("*.marca.com", "true") "]}");
    SSL_CTX *recreado = tls_ensure_cert("*.marca.com", ahora);
    cr_assert_not_null(recreado);
    cr_expect_neq(recreado, SSL_get_SSL_CTX(abierto));
    SSL_CTX_free(recreado);
    SSL_CTX_free(nuevo);
    SSL_free(abierto);
}

static void *obtener_contexto(void *arg)
{
    pthread_barrier_t *barrera = arg;
    pthread_barrier_wait(barrera);
    return tls_ensure_cert("*.marca.com", ahora);
}

Test(tls, emision_concurrente_reutiliza_un_contexto)
{
    iniciar();
    pthread_t hilos[6];
    pthread_barrier_t barrera;
    cr_assert_eq(pthread_barrier_init(&barrera, NULL, 6), 0);
    for(size_t i = 0; i < 6; i++)
        cr_assert_eq(pthread_create(&hilos[i], NULL, obtener_contexto, &barrera), 0);
    SSL_CTX *primero = NULL;
    for(size_t i = 0; i < 6; i++) {
        void *resultado;
        cr_assert_eq(pthread_join(hilos[i], &resultado), 0);
        cr_assert_not_null(resultado);
        if(i == 0)
            primero = resultado;
        else
            cr_expect_eq(resultado, primero);
        SSL_CTX_free(resultado);
    }
    pthread_barrier_destroy(&barrera);
}

static void *recargar_durante_emision(void *arg)
{
    pthread_barrier_wait(arg);
    const char *configuraciones[] = {
        "{\"urls\":[" REGLA("api.marca.com", "true") "," REGLA("*.marca.com", "true") "]}",
        "{\"urls\":[" REGLA("api.marca.com", "true") "]}"
    };
    for(size_t i = 0; i < 200; i++) {
        Configuracion *config = config_parsear(configuraciones[i % 2]);
        if(!config)
            return (void *)(intptr_t)1;
        config_instalar(config);
        usleep(500);
    }
    return NULL;
}

Test(tls, recargas_y_renovacion_en_paralelo)
{
    iniciar();
    SSL_CTX *viejo = tls_ensure_cert("*.marca.com", ahora);
    cr_assert_not_null(viejo);
    SSL *abierto = SSL_new(viejo);
    cr_assert_not_null(abierto);
    SSL_CTX_free(viejo);
    pthread_barrier_t barrera;
    cr_assert_eq(pthread_barrier_init(&barrera, NULL, 2), 0);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, recargar_durante_emision, &barrera), 0);
    pthread_barrier_wait(&barrera);
    for(int i = 0; i < 80; i++) {
        time_t fecha = ahora + (i / 2) * DIA;
        SSL_CTX *estable = tls_ensure_cert("api.marca.com", fecha);
        cr_assert_not_null(estable);
        cr_expect_eq(SSL_CTX_check_private_key(estable), 1);
        SSL_CTX_free(estable);
        SSL_CTX *variable = tls_ensure_cert("*.marca.com", fecha);
        if(variable) {
            cr_expect_eq(SSL_CTX_check_private_key(variable), 1);
            SSL_CTX_free(variable);
        }
    }
    void *resultado;
    cr_assert_eq(pthread_join(hilo, &resultado), 0);
    cr_expect_null(resultado);
    pthread_barrier_destroy(&barrera);
    cr_expect_eq(SSL_check_private_key(abierto), 1);
    SSL_free(abierto);
}

static void *aceptar(void *arg)
{
    ERR_clear_error();
    return (void *)(intptr_t)SSL_accept(arg);
}

// Estas pruebas del callback negocian por un par de sockets.
static int negociar(SSL_CTX *cliente_ctx, const char *host, Conexion *servidor, Conexion *cliente)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    struct timeval espera = {.tv_sec = 2};
    for(int i = 0; i < 2; i++) {
        cr_assert_eq(setsockopt(par[i], SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera), 0);
        cr_assert_eq(setsockopt(par[i], SOL_SOCKET, SO_SNDTIMEO, &espera, sizeof espera), 0);
    }
    *servidor = (Conexion){.fd = par[0], .ssl = tls_crear(par[0])};
    *cliente = (Conexion){.fd = par[1], .ssl = SSL_new(cliente_ctx)};
    cr_assert_not_null(servidor->ssl);
    cr_assert_not_null(cliente->ssl);
    cr_assert_eq(SSL_set_fd(cliente->ssl, cliente->fd), 1);
    if(host) {
        cr_assert_eq(SSL_set_tlsext_host_name(cliente->ssl, host), 1);
        cr_assert_eq(SSL_set1_host(cliente->ssl, host), 1);
    }
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, aceptar, servidor->ssl), 0);
    ERR_clear_error();
    int resultado_cliente = SSL_connect(cliente->ssl);
    void *resultado_servidor;
    cr_assert_eq(pthread_join(hilo, &resultado_servidor), 0);
    return resultado_cliente == 1 && (intptr_t)resultado_servidor == 1 ? 0 : -1;
}

static SSL_CTX *cliente_con_confianza(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    cr_assert_not_null(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    cr_assert_eq(X509_VERIFY_PARAM_set_flags(SSL_CTX_get0_param(ctx), X509_V_FLAG_X509_STRICT), 1);
    cr_assert_eq(SSL_CTX_load_verify_locations(ctx, ruta_cert, NULL), 1);
    return ctx;
}

static void *atender_listeners(void *arg)
{
    (void)arg;
    servidor_atender(listeners[0], listeners[1]);
    return NULL;
}

static void iniciar_listeners(void)
{
    iniciar();
    int puertos[2];
    for(int i = 0; i < 2; i++) {
        listeners[i] = servidor_abrir(0);
        cr_assert_geq(listeners[i], 0);
        struct sockaddr_in direccion;
        socklen_t largo = sizeof direccion;
        cr_assert_eq(getsockname(listeners[i], (struct sockaddr *)&direccion, &largo), 0);
        puertos[i] = ntohs(direccion.sin_port);
    }
    puerto_http = puertos[0];
    puerto_https = puertos[1];
    cr_assert_eq(pthread_create(&hilo_servidor, NULL, atender_listeners, NULL), 0);
}

static int conectar_puerto(int puerto)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    cr_assert_geq(fd, 0);
    struct timeval espera = {.tv_sec = 3};
    cr_assert_eq(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera), 0);
    cr_assert_eq(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &espera, sizeof espera), 0);
    struct sockaddr_in direccion = {.sin_family = AF_INET, .sin_port = htons((uint16_t)puerto),
                                    .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    cr_assert_eq(connect(fd, (struct sockaddr *)&direccion, sizeof direccion), 0);
    return fd;
}

static Conexion conectar_https(const char *host)
{
    SSL_CTX *ctx = cliente_con_confianza();
    Conexion cliente = {.fd = conectar_puerto(puerto_https), .ssl = SSL_new(ctx)};
    cr_assert_not_null(cliente.ssl);
    SSL_CTX_free(ctx);
    cr_assert_eq(SSL_set_fd(cliente.ssl, cliente.fd), 1);
    cr_assert_eq(SSL_set_tlsext_host_name(cliente.ssl, host), 1);
    cr_assert_eq(SSL_set1_host(cliente.ssl, host), 1);
    ERR_clear_error();
    cr_assert_eq(SSL_connect(cliente.ssl), 1);
    return cliente;
}

static void configurar_origen(int puerto, int habilitado)
{
    char texto[512];
    snprintf(texto, sizeof texto,
             "{\"urls\":[{\"id\":1,\"pattern\":\"*.marca.com\",\"tls_enabled\":%s,"
             "\"origin\":\"http://127.0.0.1:%d\",\"cache_size_bytes\":1000,"
             "\"replacement_policy\":\"lru\",\"auth\":\"none\",\"file_types\":[]}]}",
             habilitado ? "true" : "false", puerto);
    instalar(texto);
}

static void pedir_texto(Conexion *cliente, const char *peticion, char *respuesta, size_t capacidad)
{
    size_t largo = strlen(peticion);
    cr_assert_eq(conexion_escribir(cliente, peticion, largo), (ssize_t)largo);
    size_t total = 0;
    ssize_t n;
    while((n = conexion_leer(cliente, respuesta + total, capacidad - total - 1)) > 0) {
        total += (size_t)n;
        cr_assert_lt(total, capacidad - 1);
    }
    cr_assert_eq(n, 0);
    respuesta[total] = '\0';
    // Esperar el cierre del descriptor asegura que el hilo terminó de usar TLS.
    char dato;
    cr_assert_eq(recv(cliente->fd, &dato, 1, 0), 0);
    conexion_cerrar(cliente);
}

static void pedir(Conexion *cliente, const char *host, char *respuesta, size_t capacidad)
{
    char peticion[512];
    int largo = snprintf(peticion, sizeof peticion,
                         "GET /items?pagina=2 HTTP/1.1\r\nHost: %s\r\n\r\n", host);
    cr_assert_gt(largo, 0);
    cr_assert_lt((size_t)largo, sizeof peticion);
    pedir_texto(cliente, peticion, respuesta, capacidad);
}

Test(tls, listeners_http_y_https_comparten_origen)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "contenido"};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    Conexion http = {.fd = conectar_puerto(puerto_http)};
    char respuesta[1024];
    pedir(&http, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    cr_expect_not_null(strstr(respuesta, "\r\n\r\ncontenido"));
    Conexion https = conectar_https("API.MARCA.COM");
    pedir(&https, "api.marca.com:8443", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    cr_expect_not_null(strstr(respuesta, "\r\n\r\ncontenido"));
    cr_expect_eq(fake_server_connection_count(&origen), 2);
    fake_server_stop(&origen);
}

Test(tls, peticiones_mal_formadas_no_consultan_origen_y_el_servidor_continua)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    for(int tls = 0; tls < 2; tls++) {
        Conexion cliente = tls ? conectar_https("api.marca.com")
                               : (Conexion){.fd = conectar_puerto(puerto_http)};
        char respuesta[1024];
        pedir_texto(&cliente, "basura\r\n\r\n", respuesta, sizeof respuesta);
        cr_expect_not_null(strstr(respuesta, "HTTP/1.1 400 Bad Request\r\n"));
    }
    cr_expect_eq(fake_server_connection_count(&origen), 0);
    Conexion cliente = conectar_https("api.marca.com");
    char respuesta[1024];
    pedir(&cliente, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    cr_expect_eq(fake_server_connection_count(&origen), 1);
    fake_server_stop(&origen);
}

Test(tls, ambos_listeners_conservan_redirect_del_origen_sin_seguirlo)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 302};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    for(int tls = 0; tls < 2; tls++) {
        Conexion cliente = tls ? conectar_https("api.marca.com")
                               : (Conexion){.fd = conectar_puerto(puerto_http)};
        char respuesta[1024];
        pedir(&cliente, "api.marca.com", respuesta, sizeof respuesta);
        cr_expect_not_null(strstr(respuesta, "HTTP/1.1 302 Found\r\n"));
        cr_expect_not_null(strstr(respuesta, "Location: /health\r\n"));
    }
    cr_expect_eq(fake_server_connection_count(&origen), 2);
    fake_server_stop(&origen);
}

Test(tls, ambos_listeners_devuelven_502_si_el_origen_no_responde)
{
    iniciar_listeners();
    // El origen acepta y cierra sin enviar una respuesta HTTP.
    fake_server origen = {0};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    for(int tls = 0; tls < 2; tls++) {
        Conexion cliente = tls ? conectar_https("api.marca.com")
                               : (Conexion){.fd = conectar_puerto(puerto_http)};
        char respuesta[1024];
        pedir(&cliente, "api.marca.com", respuesta, sizeof respuesta);
        cr_expect_not_null(strstr(respuesta, "HTTP/1.1 502 Bad Gateway\r\n"));
    }
    cr_expect_eq(fake_server_connection_count(&origen), 2);
    fake_server_stop(&origen);
}

Test(tls, host_distinto_del_sni_rechaza_sin_consultar_origen)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    Conexion cliente = conectar_https("www.marca.com");
    char respuesta[1024];
    pedir(&cliente, "img.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 421 Misdirected Request\r\n"));
    cr_expect_eq(fake_server_connection_count(&origen), 0);
    fake_server_stop(&origen);
}

Test(tls, recarga_antes_de_http_devuelve_403_o_404_sin_origen)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    Conexion cliente = conectar_https("api.marca.com");
    configurar_origen(origen.port, 0);
    char respuesta[1024];
    pedir(&cliente, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 403 Forbidden\r\n"));
    configurar_origen(origen.port, 1);
    cliente = conectar_https("api.marca.com");
    instalar("{\"urls\":[]}");
    pedir(&cliente, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 404 Not Found\r\n"));
    cr_expect_eq(fake_server_connection_count(&origen), 0);
    fake_server_stop(&origen);
}

Test(tls, handshake_pendiente_no_bloquea_http_ni_https)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "listo"};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 1);
    int callado = conectar_puerto(puerto_https);
    Conexion http = {.fd = conectar_puerto(puerto_http)};
    char respuesta[1024];
    pedir(&http, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    Conexion https = conectar_https("api.marca.com");
    pedir(&https, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    shutdown(callado, SHUT_WR);
    // OpenSSL puede enviar una alerta TLS al recibir EOF antes del ClientHello.
    char datos[256];
    ssize_t n;
    while((n = recv(callado, datos, sizeof datos, 0)) > 0)
        cr_expect(n < 5 || memcmp(datos, "HTTP/", 5) != 0);
    cr_expect_eq(n, 0);
    close(callado);
    fake_server_stop(&origen);
}

Test(tls, http_sigue_disponible_con_tls_deshabilitado)
{
    iniciar_listeners();
    fake_server origen = {.speak_http = 1, .response_status = 200};
    cr_assert_eq(fake_server_start(&origen), 0);
    configurar_origen(origen.port, 0);
    Conexion http = {.fd = conectar_puerto(puerto_http)};
    char respuesta[1024];
    pedir(&http, "api.marca.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "HTTP/1.1 200 OK\r\n"));
    cr_expect_eq(fake_server_connection_count(&origen), 1);
    fake_server_stop(&origen);
}

Test(tls, listener_rechaza_sni_y_datos_invalidos_sin_responder_http)
{
    iniciar_listeners();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    cr_assert_not_null(ctx);
    const char *hosts[] = {NULL, "otro.com", "sin-tls.com"};
    for(size_t i = 0; i < 3; i++) {
        Conexion cliente = {.fd = conectar_puerto(puerto_https), .ssl = SSL_new(ctx)};
        cr_assert_not_null(cliente.ssl);
        cr_assert_eq(SSL_set_fd(cliente.ssl, cliente.fd), 1);
        if(hosts[i])
            cr_assert_eq(SSL_set_tlsext_host_name(cliente.ssl, hosts[i]), 1);
        ERR_clear_error();
        cr_expect_leq(SSL_connect(cliente.ssl), 0);
        char dato;
        cr_expect_eq(recv(cliente.fd, &dato, 1, 0), 0);
        conexion_cerrar(&cliente);
    }
    SSL_CTX_free(ctx);
    int fd = conectar_puerto(puerto_https);
    const char *texto = "GET / HTTP/1.1\r\nHost: api.marca.com\r\n\r\n";
    cr_assert_eq(send(fd, texto, strlen(texto), MSG_NOSIGNAL), (ssize_t)strlen(texto));
    shutdown(fd, SHUT_WR);
    char respuesta[256] = {0};
    ssize_t n = recv(fd, respuesta, sizeof respuesta - 1, 0);
    cr_expect(n <= 0 || strstr(respuesta, "HTTP/1.1") == NULL);
    close(fd);
    // Después de esos fallos el listener sigue atendiendo una sesión válida.
    Conexion cliente = conectar_https("api.marca.com");
    pedir(&cliente, "otro.com", respuesta, sizeof respuesta);
    cr_expect_not_null(strstr(respuesta, "421 Misdirected Request"));
}

static void *aceptar_con_plazo(void *arg)
{
    return (void *)(intptr_t)tls_aceptar(arg);
}

Test(tls, handshake_con_plazo_restaura_socket_bloqueante)
{
    iniciar();
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    Conexion servidor = {.fd = par[0]};
    SSL_CTX *ctx = cliente_con_confianza();
    Conexion cliente = {.fd = par[1], .ssl = SSL_new(ctx)};
    cr_assert_not_null(cliente.ssl);
    SSL_CTX_free(ctx);
    cr_assert_eq(SSL_set_fd(cliente.ssl, cliente.fd), 1);
    cr_assert_eq(SSL_set_tlsext_host_name(cliente.ssl, "api.marca.com"), 1);
    cr_assert_eq(SSL_set1_host(cliente.ssl, "api.marca.com"), 1);
    int flags = fcntl(servidor.fd, F_GETFL);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, aceptar_con_plazo, &servidor), 0);
    ERR_clear_error();
    cr_assert_eq(SSL_connect(cliente.ssl), 1);
    void *resultado;
    cr_assert_eq(pthread_join(hilo, &resultado), 0);
    cr_expect_eq((intptr_t)resultado, 0);
    cr_expect_eq(fcntl(servidor.fd, F_GETFL), flags);
    cr_expect_eq(conexion_escribir(&cliente, "x", 1), 1);
    char dato;
    cr_expect_eq(conexion_leer(&servidor, &dato, 1), 1);
    cr_expect_eq(dato, 'x');
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
}

typedef struct {
    int fd;
    char hello[4096];
    int largo;
} ClienteLento;

static void *enviar_lentamente(void *arg)
{
    ClienteLento *cliente = arg;
    for(int i = 0; i < cliente->largo; i++) {
        if(send(cliente->fd, cliente->hello + i, 1, MSG_NOSIGNAL) != 1)
            break;
        usleep(100000);
    }
    close(cliente->fd);
    return NULL;
}

Test(tls, handshake_lento_no_reinicia_el_plazo_total)
{
    iniciar();
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    ClienteLento lento = {.fd = par[1]};
    // Obtener un ClientHello real y enviarlo a un byte cada 100 ms.
    SSL_CTX *ctx = cliente_con_confianza();
    SSL *cliente = SSL_new(ctx);
    cr_assert_not_null(cliente);
    SSL_CTX_free(ctx);
    BIO *entrada = BIO_new(BIO_s_mem()), *salida = BIO_new(BIO_s_mem());
    cr_assert_not_null(entrada);
    cr_assert_not_null(salida);
    SSL_set_bio(cliente, entrada, salida);
    SSL_set_connect_state(cliente);
    cr_assert_eq(SSL_set_tlsext_host_name(cliente, "api.marca.com"), 1);
    ERR_clear_error();
    int n = SSL_do_handshake(cliente);
    cr_assert_eq(SSL_get_error(cliente, n), SSL_ERROR_WANT_READ);
    lento.largo = BIO_read(salida, lento.hello, sizeof lento.hello);
    cr_assert_gt(lento.largo, 100);
    SSL_free(cliente);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, enviar_lentamente, &lento), 0);
    Conexion servidor = {.fd = par[0]};
    int flags = fcntl(servidor.fd, F_GETFL);
    struct timespec inicio, fin;
    cr_assert_eq(clock_gettime(CLOCK_MONOTONIC, &inicio), 0);
    cr_expect_eq(tls_aceptar(&servidor), -1);
    cr_assert_eq(clock_gettime(CLOCK_MONOTONIC, &fin), 0);
    double segundos = (double)(fin.tv_sec - inicio.tv_sec) +
                      (double)(fin.tv_nsec - inicio.tv_nsec) / 1000000000.0;
    cr_expect_geq(segundos, 9.5);
    cr_expect_lt(segundos, 12.0);
    cr_expect_eq(fcntl(servidor.fd, F_GETFL), flags);
    conexion_cerrar(&servidor);
    cr_assert_eq(pthread_join(hilo, NULL), 0);
}

Test(tls, sni_selecciona_certificado_y_reutiliza_wildcard)
{
    iniciar();
    SSL_CTX *ctx = cliente_con_confianza();
    Conexion servidor1, cliente1, servidor2, cliente2, servidor3, cliente3;
    cr_assert_eq(negociar(ctx, "WWW.MARCA.COM", &servidor1, &cliente1), 0);
    cr_assert_eq(negociar(ctx, "img.marca.com", &servidor2, &cliente2), 0);
    cr_assert_eq(negociar(ctx, "api.marca.com", &servidor3, &cliente3), 0);
    cr_expect_eq(SSL_get_SSL_CTX(servidor1.ssl), SSL_get_SSL_CTX(servidor2.ssl));
    cr_expect_neq(SSL_get_SSL_CTX(servidor1.ssl), SSL_get_SSL_CTX(servidor3.ssl));
    verificar_certificado(SSL_get_SSL_CTX(servidor1.ssl), "*.marca.com");
    verificar_certificado(SSL_get_SSL_CTX(servidor3.ssl), "api.marca.com");
    const unsigned char *alpn;
    unsigned int largo;
    SSL_get0_alpn_selected(cliente1.ssl, &alpn, &largo);
    cr_expect_eq(largo, 0);
    cr_expect_eq(conexion_escribir(&cliente1, "hola", 4), 4);
    char recibida[4];
    cr_expect_eq(conexion_leer(&servidor1, recibida, sizeof recibida), 4);
    cr_expect_eq(memcmp(recibida, "hola", 4), 0);
    conexion_cerrar(&servidor1);
    conexion_cerrar(&cliente1);
    conexion_cerrar(&servidor2);
    conexion_cerrar(&cliente2);
    conexion_cerrar(&servidor3);
    conexion_cerrar(&cliente3);
    SSL_CTX_free(ctx);
}

Test(tls, wildcard_acepta_raiz_y_subdominios_profundos_con_certificados_validos)
{
    iniciar();
    SSL_CTX *ctx = cliente_con_confianza();
    const char *hosts[] = {"marca.com", "a.b.marca.com"};
    for(size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++) {
        Conexion servidor, cliente;
        cr_assert_eq(negociar(ctx, hosts[i], &servidor, &cliente), 0);
        verificar_certificado(SSL_get_SSL_CTX(servidor.ssl), hosts[i]);
        conexion_cerrar(&servidor);
        conexion_cerrar(&cliente);
    }
    instalar("{\"urls\":[" REGLA("*.marca.com", "false") "]}");
    for(size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++)
        cr_expect_null(tls_ensure_cert(hosts[i], time(NULL)));
    SSL_CTX_free(ctx);
}

Test(tls, sni_rechaza_ausentes_desconocidos_deshabilitados_e_invalidos)
{
    iniciar();
    SSL_CTX *ctx = cliente_con_confianza();
    // Sin verificación del cliente, el rechazo sigue dependiendo del servidor.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    const char *hosts[] = {NULL, "otro.com", "sin-tls.com",
                           "*.marca.com", "-mal.marca.com", "mal_.marca.com", "a..marca.com",
                           "www.marca.com:443", "www.marca.com.", "127.0.0.1"};
    for(size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++) {
        Conexion servidor, cliente;
        cr_expect_eq(negociar(ctx, hosts[i], &servidor, &cliente), -1, "SNI: %s", hosts[i]);
        conexion_cerrar(&servidor);
        conexion_cerrar(&cliente);
    }
    // Una exacta deshabilitada gana a la wildcard habilitada.
    instalar("{\"urls\":[" REGLA("*.marca.com", "true") "," REGLA("api.marca.com", "false") "]}");
    Conexion servidor, cliente;
    cr_expect_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
    SSL_CTX_free(ctx);
}

Test(tls, handshake_sin_configuracion)
{
    cr_assert_eq(tls_iniciar(ruta_cert, ruta_llave), 0);
    SSL_CTX *ctx = cliente_con_confianza();
    Conexion servidor, cliente;
    cr_expect_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
    SSL_CTX_free(ctx);
}

Test(tls, recarga_rechaza_nuevas_sesiones_aunque_exista_certificado)
{
    iniciar();
    SSL_CTX *ctx = cliente_con_confianza();
    Conexion servidor_abierto, cliente_abierto;
    cr_assert_eq(negociar(ctx, "api.marca.com", &servidor_abierto, &cliente_abierto), 0);
    const char *configuraciones[] = {
        "{\"urls\":[" REGLA("api.marca.com", "false") "]}", "{\"urls\":[]}"
    };
    for(size_t i = 0; i < 2; i++) {
        instalar(configuraciones[i]);
        Conexion servidor, cliente;
        cr_expect_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), -1);
        conexion_cerrar(&servidor);
        conexion_cerrar(&cliente);
    }
    cr_expect_eq(conexion_escribir(&cliente_abierto, "x", 1), 1);
    char dato;
    cr_expect_eq(conexion_leer(&servidor_abierto, &dato, 1), 1);
    cr_expect_eq(dato, 'x');
    conexion_cerrar(&servidor_abierto);
    conexion_cerrar(&cliente_abierto);
    SSL_CTX_free(ctx);
}

Test(tls, cliente_sin_confianza_rechaza_el_certificado)
{
    iniciar();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    cr_assert_not_null(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    Conexion servidor, cliente;
    cr_expect_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), -1);
    cr_expect_neq(SSL_get_verify_result(cliente.ssl), X509_V_OK);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
    SSL_CTX_free(ctx);
}

Test(tls, requiere_tls_12_y_permite_tls_13)
{
    iniciar();
    SSL_CTX *ctx = cliente_con_confianza();
    const int versiones[] = {TLS1_2_VERSION, TLS1_3_VERSION};
    for(size_t i = 0; i < 2; i++) {
        cr_assert_eq(SSL_CTX_set_min_proto_version(ctx, versiones[i]), 1);
        cr_assert_eq(SSL_CTX_set_max_proto_version(ctx, versiones[i]), 1);
        Conexion servidor, cliente;
        cr_assert_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), 0);
        cr_expect_eq(SSL_version(cliente.ssl), versiones[i]);
        conexion_cerrar(&servidor);
        conexion_cerrar(&cliente);
    }
    // TLS 1.1 llega al servidor con cifrados habilitados para esa versión.
    cr_assert_eq(SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION), 1);
    cr_assert_eq(SSL_CTX_set_max_proto_version(ctx, TLS1_1_VERSION), 1);
    cr_assert_eq(SSL_CTX_set_cipher_list(ctx, "DEFAULT:@SECLEVEL=0"), 1);
    Conexion servidor, cliente;
    cr_expect_eq(negociar(ctx, "api.marca.com", &servidor, &cliente), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
    SSL_CTX_free(ctx);
}

Test(tls, rechaza_archivos_ausentes_o_invalidos)
{
    cr_expect_eq(tls_iniciar("/no-existe/ca.pem", ruta_llave), -1);
    cr_expect_eq(tls_iniciar(ruta_cert, "/no-existe/key.pem"), -1);
    FILE *archivo = fopen(ruta_cert, "w");
    cr_assert_not_null(archivo);
    fputs("no es PEM", archivo);
    fclose(archivo);
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
    guardar_ca();
    archivo = fopen(ruta_llave, "w");
    cr_assert_not_null(archivo);
    fputs("no es PEM", archivo);
    fclose(archivo);
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
}

Test(tls, rechaza_ca_vencida_o_futura)
{
    cr_assert_not_null(ASN1_TIME_set(X509_getm_notAfter(ca), ahora - 1));
    guardar_ca();
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
    cr_assert_not_null(ASN1_TIME_adj(X509_getm_notAfter(ca), ahora, 365, 0));
    cr_assert_not_null(ASN1_TIME_set(X509_getm_notBefore(ca), ahora + DIA));
    guardar_ca();
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
}

Test(tls, rechaza_certificado_sin_capacidad_de_ca)
{
    // Sustituir CA:TRUE por CA:FALSE, sin dejar extensiones duplicadas.
    int indice = X509_get_ext_by_NID(ca, NID_basic_constraints, -1);
    X509_EXTENSION_free(X509_delete_ext(ca, indice));
    extension(ca, NID_basic_constraints, "critical,CA:FALSE");
    guardar_ca();
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
}

Test(tls, rechaza_ca_sin_uso_de_firma)
{
    int indice = X509_get_ext_by_NID(ca, NID_key_usage, -1);
    X509_EXTENSION_free(X509_delete_ext(ca, indice));
    extension(ca, NID_key_usage, "critical,digitalSignature");
    guardar_ca();
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
}

Test(tls, rechaza_llave_que_no_corresponde_a_la_ca)
{
    EVP_PKEY *otra = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
    cr_assert_not_null(otra);
    FILE *archivo = fopen(ruta_llave, "w");
    cr_assert_not_null(archivo);
    cr_assert_eq(PEM_write_PrivateKey(archivo, otra, NULL, NULL, 0, NULL, NULL), 1);
    fclose(archivo);
    EVP_PKEY_free(otra);
    cr_expect_eq(tls_iniciar(ruta_cert, ruta_llave), -1);
}
