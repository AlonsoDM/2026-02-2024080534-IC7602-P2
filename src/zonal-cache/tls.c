#include "tls.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

#define MAX_NOMBRE 253
#define SEGUNDOS_DIA (24 * 60 * 60)
#define DIAS_CERTIFICADO 30
#define TIMEOUT_HANDSHAKE_MS 10000

typedef struct Certificado {
    char *patron;
    SSL_CTX *contexto;
    struct Certificado *siguiente;
} Certificado;

static pthread_mutex_t candado = PTHREAD_MUTEX_INITIALIZER;
static X509 *ca;
static EVP_PKEY *llave_ca;
static SSL_CTX *inicial;
static Certificado *certificados;

// SNI acepta nombres DNS; el patrón también puede comenzar con "*.".
static int normalizar(const char *nombre, char *destino, int wildcard)
{
    if(!nombre)
        return -1;
    size_t largo = strlen(nombre);
    if(largo == 0 || largo > MAX_NOMBRE)
        return -1;
    struct in_addr ip;
    if(inet_pton(AF_INET, nombre, &ip) == 1)
        return -1;
    size_t inicio = wildcard && strncmp(nombre, "*.", 2) == 0 ? 2 : 0;
    if(inicio) {
        destino[0] = '*';
        destino[1] = '.';
    }
    size_t etiqueta = 0;
    for(size_t i = inicio; i < largo; i++) {
        unsigned char c = (unsigned char)nombre[i];
        if(c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if(c == '.') {
            if(etiqueta == 0 || nombre[i - 1] == '-')
                return -1;
            etiqueta = 0;
        } else {
            if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
               (etiqueta == 0 && c == '-') || ++etiqueta > 63)
                return -1;
        }
        destino[i] = (char)c;
    }
    if(etiqueta == 0 || nombre[largo - 1] == '-')
        return -1;
    destino[largo] = '\0';
    return 0;
}

static int vigente(const X509 *cert, time_t ahora)
{
    return X509_cmp_time(X509_get0_notBefore(cert), &ahora) < 0 &&
           X509_cmp_time(X509_get0_notAfter(cert), &ahora) > 0;
}

static SSL_CTX *crear_contexto(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if(!ctx)
        return NULL;
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    // Cada handshake vuelve a validar SNI y la configuración vigente.
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_num_tickets(ctx, 0);
    return ctx;
}

static int agregar_extension(X509 *cert, int tipo, const char *valor)
{
    X509V3_CTX contexto;
    X509V3_set_ctx(&contexto, ca, cert, NULL, NULL, 0);
    X509_EXTENSION *extension = X509V3_EXT_conf_nid(NULL, &contexto, tipo, valor);
    if(!extension)
        return -1;
    int resultado = X509_add_ext(cert, extension, -1);
    X509_EXTENSION_free(extension);
    return resultado == 1 ? 0 : -1;
}

static SSL_CTX *emitir(const char *patron, time_t ahora)
{
    SSL_CTX *ctx = NULL;
    EVP_PKEY *llave = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
    X509 *cert = X509_new();
    BIGNUM *serie = BN_new();
    char san[MAX_NOMBRE + 5];
    snprintf(san, sizeof san, "DNS:%s", patron);
    if(!llave || !cert || !serie ||
       BN_rand(serie, 128, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY) != 1 ||
       !BN_to_ASN1_INTEGER(serie, X509_get_serialNumber(cert)) ||
       X509_set_version(cert, 2) != 1 || X509_set_pubkey(cert, llave) != 1 ||
       X509_set_issuer_name(cert, X509_get_subject_name(ca)) != 1 ||
       !ASN1_TIME_set(X509_getm_notBefore(cert), ahora) ||
       !ASN1_TIME_adj(X509_getm_notAfter(cert), ahora, DIAS_CERTIFICADO, 0))
        goto error;
    if(ASN1_TIME_compare(X509_get0_notAfter(cert), X509_get0_notAfter(ca)) > 0 &&
       X509_set1_notAfter(cert, X509_get0_notAfter(ca)) != 1)
        goto error;
    if(X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                                 (const unsigned char *)"Zonal Cache", -1, -1, 0) != 1 ||
       agregar_extension(cert, NID_basic_constraints, "critical,CA:FALSE") != 0 ||
       agregar_extension(cert, NID_key_usage, "critical,digitalSignature,keyEncipherment") != 0 ||
       agregar_extension(cert, NID_ext_key_usage, "serverAuth") != 0 ||
       agregar_extension(cert, NID_subject_key_identifier, "hash") != 0 ||
       agregar_extension(cert, NID_authority_key_identifier, "keyid,issuer") != 0 ||
       agregar_extension(cert, NID_subject_alt_name, san) != 0 ||
       X509_sign(cert, llave_ca, EVP_sha256()) <= 0)
        goto error;
    ctx = crear_contexto();
    if(!ctx || SSL_CTX_use_certificate(ctx, cert) != 1 || SSL_CTX_use_PrivateKey(ctx, llave) != 1)
        goto error;
    BN_free(serie);
    X509_free(cert);
    EVP_PKEY_free(llave);
    return ctx;

error:
    SSL_CTX_free(ctx);
    BN_free(serie);
    X509_free(cert);
    EVP_PKEY_free(llave);
    log_error("No se pudo emitir el certificado TLS para %s", patron);
    return NULL;
}

static Certificado *buscar(const char *patron)
{
    Certificado *entrada = certificados;
    while(entrada && strcmp(entrada->patron, patron) != 0)
        entrada = entrada->siguiente;
    return entrada;
}

static void liberar_entrada(Certificado *entrada)
{
    SSL_CTX_free(entrada->contexto);
    free(entrada->patron);
    free(entrada);
}

static bool habilitado(const char *nombre)
{
    if(strncmp(nombre, "*.", 2) == 0)
        return config_patron_tls_habilitado(nombre);
    const UrlConfig *url = config_buscar(nombre);
    bool valido = url && url->tls_enabled;
    if(url)
        config_soltar(url);
    return valido;
}

// Se retira solo la referencia de la tabla; cada SSL conserva la suya.
static void retirar(void)
{
    Certificado **posicion = &certificados;
    while(*posicion) {
        Certificado *entrada = *posicion;
        if(habilitado(entrada->patron)) {
            posicion = &entrada->siguiente;
        } else {
            *posicion = entrada->siguiente;
            liberar_entrada(entrada);
        }
    }
}

SSL_CTX *tls_ensure_cert(const char *patron, time_t ahora)
{
    char nombre[MAX_NOMBRE + 1];
    if(normalizar(patron, nombre, 1) != 0 || !habilitado(nombre))
        return NULL;
    SSL_CTX *ctx = NULL;
    pthread_mutex_lock(&candado);
    if(!ca || !vigente(ca, ahora))
        goto fin;
    Certificado *entrada = buscar(nombre);
    time_t renovar = ahora + SEGUNDOS_DIA;
    if(entrada && vigente(SSL_CTX_get0_certificate(entrada->contexto), ahora) &&
       X509_cmp_time(X509_get0_notAfter(SSL_CTX_get0_certificate(entrada->contexto)), &renovar) > 0) {
        ctx = entrada->contexto;
    } else {
        retirar();
        // La limpieza puede haber retirado la entrada que íbamos a renovar.
        entrada = buscar(nombre);
        SSL_CTX *nuevo = emitir(nombre, ahora);
        if(!nuevo)
            goto fin;
        if(!entrada) {
            entrada = calloc(1, sizeof *entrada);
            if(!entrada) {
                SSL_CTX_free(nuevo);
                goto fin;
            }
            entrada->patron = strdup(nombre);
            if(!entrada->patron) {
                free(entrada);
                SSL_CTX_free(nuevo);
                goto fin;
            }
            entrada->siguiente = certificados;
            certificados = entrada;
        } else {
            SSL_CTX_free(entrada->contexto);
        }
        entrada->contexto = nuevo;
        ctx = nuevo;
    }
    if(SSL_CTX_up_ref(ctx) != 1)
        ctx = NULL;
fin:
    pthread_mutex_unlock(&candado);
    return ctx;
}

static int seleccionar_sni(SSL *ssl, int *alerta, void *arg)
{
    (void)arg;
    char nombre[MAX_NOMBRE + 1];
    *alerta = SSL_AD_UNRECOGNIZED_NAME;
    if(normalizar(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name), nombre, 0) != 0)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    const UrlConfig *url = config_buscar(nombre);
    if(!url)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    char patron[MAX_NOMBRE + 1];
    size_t largo = strlen(url->patron);
    int valido = url->tls_enabled && largo <= MAX_NOMBRE;
    if(valido)
        memcpy(patron, url->patron, largo + 1);
    config_soltar(url);
    if(!valido)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    SSL_CTX *ctx = tls_ensure_cert(patron, time(NULL));
    // Un wildcard TLS no cubre el raíz ni subdominios de varios niveles.
    if(ctx && X509_check_host(SSL_CTX_get0_certificate(ctx), nombre, 0, 0, NULL) != 1) {
        SSL_CTX_free(ctx);
        ctx = tls_ensure_cert(nombre, time(NULL));
    }
    if(!ctx) {
        *alerta = SSL_AD_INTERNAL_ERROR;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    SSL_CTX *seleccionado = SSL_set_SSL_CTX(ssl, ctx);
    SSL_CTX_free(ctx);
    if(!seleccionado) {
        *alerta = SSL_AD_INTERNAL_ERROR;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    return SSL_TLSEXT_ERR_OK;
}

int tls_iniciar(const char *cert_file, const char *key_file)
{
    BIO *archivo = BIO_new_file(cert_file, "r");
    X509 *cert = archivo ? PEM_read_bio_X509(archivo, NULL, NULL, NULL) : NULL;
    BIO_free(archivo);
    archivo = BIO_new_file(key_file, "r");
    EVP_PKEY *llave = archivo ? PEM_read_bio_PrivateKey(archivo, NULL, NULL, "") : NULL;
    BIO_free(archivo);
    time_t ahora = time(NULL);
    SSL_CTX *ctx = NULL;
    if(!cert || !llave || !vigente(cert, ahora) || X509_check_ca(cert) != 1 ||
       X509_check_private_key(cert, llave) != 1)
        goto error;
    ctx = crear_contexto();
    if(!ctx || SSL_CTX_set_tlsext_servername_callback(ctx, seleccionar_sni) != 1)
        goto error;
    ca = cert;
    llave_ca = llave;
    inicial = ctx;
    return 0;
error:
    log_error("No se pudo cargar una CA TLS vigente con su llave de firma");
    SSL_CTX_free(ctx);
    X509_free(cert);
    EVP_PKEY_free(llave);
    return -1;
}

SSL *tls_crear(int fd)
{
    SSL *ssl = SSL_new(inicial);
    if(ssl && SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

static int64_t tiempo_ms(void)
{
    struct timespec ahora;
    if(clock_gettime(CLOCK_MONOTONIC, &ahora) != 0)
        return -1;
    return (int64_t)ahora.tv_sec * 1000 + ahora.tv_nsec / 1000000;
}

int tls_aceptar(Conexion *conexion)
{
    int64_t inicio = tiempo_ms();
    int flags = fcntl(conexion->fd, F_GETFL);
    if(inicio < 0 || flags < 0 || fcntl(conexion->fd, F_SETFL, flags | O_NONBLOCK) != 0)
        return -1;
    conexion->ssl = tls_crear(conexion->fd);
    int resultado = -1;
    int64_t fin = inicio + TIMEOUT_HANDSHAKE_MS;
    while(conexion->ssl) {
        int64_t ahora = tiempo_ms();
        if(ahora < 0 || ahora >= fin)
            break;
        ERR_clear_error();
        int n = SSL_accept(conexion->ssl);
        if(n == 1) {
            ahora = tiempo_ms();
            if(ahora >= 0 && ahora < fin)
                resultado = 0;
            break;
        }
        int error = SSL_get_error(conexion->ssl, n);
        if(error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
            break;
        ahora = tiempo_ms();
        if(ahora < 0 || ahora >= fin)
            break;
        struct pollfd espera = {.fd = conexion->fd,
                                .events = error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT};
        int listo = poll(&espera, 1, (int)(fin - ahora));
        if(listo < 0 && errno == EINTR)
            continue;
        if(listo <= 0 || (espera.revents & (POLLERR | POLLHUP | POLLNVAL)))
            break;
    }
    if(fcntl(conexion->fd, F_SETFL, flags) != 0)
        resultado = -1;
    if(resultado != 0 && conexion->ssl)
        SSL_set_shutdown(conexion->ssl, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
    return resultado;
}

void tls_liberar(void)
{
    while(certificados) {
        Certificado *entrada = certificados;
        certificados = entrada->siguiente;
        liberar_entrada(entrada);
    }
    SSL_CTX_free(inicial);
    X509_free(ca);
    EVP_PKEY_free(llave_ca);
    inicial = NULL;
    ca = NULL;
    llave_ca = NULL;
}
