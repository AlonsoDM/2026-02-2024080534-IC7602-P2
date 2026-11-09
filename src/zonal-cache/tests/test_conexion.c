#include <criterion/criterion.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "conexion.h"
#include "http.h"

static void ignorar_sigpipe(void)
{
    cr_assert_neq(signal(SIGPIPE, SIG_IGN), SIG_ERR);
}

TestSuite(conexion, .init = ignorar_sigpipe, .timeout = 5);

// Certificado exclusivo de las pruebas, generado y confiado en memoria.
static void certificado_de_prueba(SSL_CTX *servidor, SSL_CTX *cliente)
{
    EVP_PKEY *llave = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
    X509 *cert = X509_new();
    cr_assert_not_null(llave);
    cr_assert_not_null(cert);
    cr_assert_eq(X509_set_version(cert, 2), 1);
    cr_assert_eq(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1), 1);
    cr_assert_not_null(X509_gmtime_adj(X509_getm_notBefore(cert), -60));
    cr_assert_not_null(X509_gmtime_adj(X509_getm_notAfter(cert), 3600));
    cr_assert_eq(X509_set_pubkey(cert, llave), 1);
    X509_NAME *nombre = X509_get_subject_name(cert);
    cr_assert_eq(X509_NAME_add_entry_by_txt(nombre, "CN", MBSTRING_ASC,
                                           (const unsigned char *)"localhost", -1, -1, 0), 1);
    cr_assert_eq(X509_set_issuer_name(cert, nombre), 1);
    X509_EXTENSION *san = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, "DNS:localhost");
    cr_assert_not_null(san);
    cr_assert_eq(X509_add_ext(cert, san, -1), 1);
    X509_EXTENSION_free(san);
    cr_assert_gt(X509_sign(cert, llave, EVP_sha256()), 0);
    cr_assert_eq(SSL_CTX_use_certificate(servidor, cert), 1);
    cr_assert_eq(SSL_CTX_use_PrivateKey(servidor, llave), 1);
    cr_assert_eq(X509_STORE_add_cert(SSL_CTX_get_cert_store(cliente), cert), 1);
    X509_free(cert);
    EVP_PKEY_free(llave);
}

static void *aceptar(void *arg)
{
    return (void *)(intptr_t)SSL_accept(arg);
}

// El handshake pertenece a la preparación de la prueba, no al servidor HTTP.
static void abrir_tls(Conexion *servidor, Conexion *cliente)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    struct timeval espera = {.tv_sec = 1};
    for(int i = 0; i < 2; i++) {
        cr_assert_eq(setsockopt(par[i], SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera), 0);
        cr_assert_eq(setsockopt(par[i], SOL_SOCKET, SO_SNDTIMEO, &espera, sizeof espera), 0);
    }
    SSL_CTX *ctx_servidor = SSL_CTX_new(TLS_server_method());
    SSL_CTX *ctx_cliente = SSL_CTX_new(TLS_client_method());
    cr_assert_not_null(ctx_servidor);
    cr_assert_not_null(ctx_cliente);
    cr_assert_eq(SSL_CTX_set_min_proto_version(ctx_servidor, TLS1_2_VERSION), 1);
    certificado_de_prueba(ctx_servidor, ctx_cliente);
    SSL_CTX_set_verify(ctx_cliente, SSL_VERIFY_PEER, NULL);
    *servidor = (Conexion){.fd = par[0], .ssl = SSL_new(ctx_servidor)};
    *cliente = (Conexion){.fd = par[1], .ssl = SSL_new(ctx_cliente)};
    cr_assert_not_null(servidor->ssl);
    cr_assert_not_null(cliente->ssl);
    // Cada SSL conserva su referencia al contexto.
    SSL_CTX_free(ctx_servidor);
    SSL_CTX_free(ctx_cliente);
    cr_assert_eq(SSL_set_fd(servidor->ssl, servidor->fd), 1);
    cr_assert_eq(SSL_set_fd(cliente->ssl, cliente->fd), 1);
    cr_assert_eq(SSL_set1_host(cliente->ssl, "localhost"), 1);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, aceptar, servidor->ssl), 0);
    cr_assert_eq(SSL_connect(cliente->ssl), 1);
    void *resultado;
    cr_assert_eq(pthread_join(hilo, &resultado), 0);
    cr_assert_eq((intptr_t)resultado, 1);
    cr_assert_eq(SSL_get_verify_result(cliente->ssl), X509_V_OK);
}

Test(conexion, http_sobre_tls_con_cuerpo_y_escrituras_parciales)
{
    Conexion servidor, cliente;
    abrir_tls(&servidor, &cliente);
    const char *cabecera = "POST /items HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n";
    cr_assert_eq(conexion_escribir(&cliente, cabecera, strlen(cabecera)), (ssize_t)strlen(cabecera));
    cr_assert_eq(conexion_escribir(&cliente, "a\0", 2), 2);
    cr_assert_eq(conexion_escribir(&cliente, "bc", 2), 2);
    // Los errores de una operación anterior del hilo no deben alterar SSL_get_error.
    ERR_raise(ERR_LIB_SSL, ERR_R_INTERNAL_ERROR);
    Peticion p;
    cr_assert_eq(http_leer_peticion(&servidor, &p), 0);
    cr_expect_str_eq(p.metodo, "POST");
    cr_expect_str_eq(p.ruta, "/items");
    cr_expect_str_eq(p.host, "localhost");
    cr_assert_eq(p.largo_cuerpo, 4);
    cr_expect_eq(memcmp(p.cuerpo, "a\0bc", 4), 0);
    http_liberar_peticion(&p);

    // Una respuesta mayor que un registro TLS obliga a enviar_todo a continuar.
    SSL_set_mode(servidor.ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    Respuesta r = {.codigo = 200, .cuerpo = malloc(20000), .largo_cuerpo = 20000};
    cr_assert_not_null(r.cuerpo);
    memset(r.cuerpo, 'x', r.largo_cuerpo);
    ERR_raise(ERR_LIB_SSL, ERR_R_INTERNAL_ERROR);
    cr_assert_eq(http_enviar_respuesta(&servidor, &r), 0);
    conexion_cerrar(&servidor);
    cr_expect_eq(servidor.fd, -1);
    cr_expect_null(servidor.ssl);

    char recibida[20512];
    size_t total = 0;
    ssize_t n;
    while((n = conexion_leer(&cliente, recibida + total, sizeof recibida - total)) > 0)
        total += (size_t)n;
    cr_assert_eq(n, 0); // close_notify
    const char *esperada = "HTTP/1.1 200 OK\r\nContent-Length: 20000\r\nConnection: close\r\n\r\n";
    cr_assert_eq(total, strlen(esperada) + r.largo_cuerpo);
    cr_expect_eq(memcmp(recibida, esperada, strlen(esperada)), 0);
    cr_expect_eq(memcmp(recibida + strlen(esperada), r.cuerpo, r.largo_cuerpo), 0);
    http_liberar_respuesta(&r);
    conexion_cerrar(&cliente);
    conexion_cerrar(&servidor);
}

Test(conexion, lectura_tls_con_timeout)
{
    Conexion servidor, cliente;
    abrir_tls(&servidor, &cliente);
    struct timeval espera = {.tv_usec = 100000};
    cr_assert_eq(setsockopt(servidor.fd, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera), 0);
    char dato;
    cr_expect_eq(conexion_leer(&servidor, &dato, 1), -1);
    cr_expect_eq(conexion_escribir(&servidor, "x", 1), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
}

Test(conexion, lectura_tls_sin_close_notify_es_error)
{
    Conexion servidor, cliente;
    abrir_tls(&servidor, &cliente);
    // Quitar el socket sin cierre TLS simula un cliente que se desconecta.
    close(cliente.fd);
    cliente.fd = -1;
    char dato;
    cr_expect_eq(conexion_leer(&servidor, &dato, 1), -1);
    cr_expect_eq(conexion_escribir(&servidor, "x", 1), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
}

Test(conexion, escritura_tls_con_cliente_desconectado)
{
    Conexion servidor, cliente;
    abrir_tls(&servidor, &cliente);
    close(cliente.fd);
    cliente.fd = -1;
    cr_expect_eq(conexion_escribir(&servidor, "x", 1), -1);
    conexion_cerrar(&servidor);
    conexion_cerrar(&cliente);
}

Test(conexion, cerrar_sin_esperar_al_cliente)
{
    Conexion servidor, cliente;
    abrir_tls(&servidor, &cliente);
    // El cliente no lee ni envía close_notify; cerrar debe regresar igualmente.
    struct timeval sin_timeout = {0};
    cr_assert_eq(setsockopt(servidor.fd, SOL_SOCKET, SO_SNDTIMEO, &sin_timeout, sizeof sin_timeout), 0);
    cr_assert_eq(setsockopt(servidor.fd, SOL_SOCKET, SO_RCVTIMEO, &sin_timeout, sizeof sin_timeout), 0);
    // Llenar el socket comprueba que tampoco se bloquea al enviar el aviso.
    char datos[4096] = {0};
    while(send(servidor.fd, datos, sizeof datos, MSG_DONTWAIT | MSG_NOSIGNAL) > 0) {}
    cr_assert_eq(errno, EAGAIN);
    int fd = servidor.fd;
    conexion_cerrar(&servidor);
    cr_expect_eq(servidor.fd, -1);
    cr_expect_null(servidor.ssl);
    cr_expect_eq(fcntl(fd, F_GETFD), -1);
    // El descriptor puede reutilizarse antes de un segundo cierre.
    cr_assert_eq(dup2(cliente.fd, fd), fd);
    conexion_cerrar(&servidor);
    cr_expect_geq(fcntl(fd, F_GETFD), 0);
    close(fd);
    conexion_cerrar(&cliente);
}

Test(conexion, cerrar_tls_sin_handshake)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    cr_assert_not_null(ctx);
    Conexion conexion = {.fd = par[0], .ssl = SSL_new(ctx)};
    cr_assert_not_null(conexion.ssl);
    SSL_CTX_free(ctx);
    cr_assert_eq(SSL_set_fd(conexion.ssl, conexion.fd), 1);
    conexion_cerrar(&conexion);
    cr_expect_eq(conexion.fd, -1);
    cr_expect_null(conexion.ssl);
    char dato;
    cr_expect_eq(read(par[1], &dato, 1), 0);
    close(par[1]);
}
