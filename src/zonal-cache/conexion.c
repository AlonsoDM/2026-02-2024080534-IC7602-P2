#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <unistd.h>

#include "conexion.h"

static ssize_t resultado_tls(SSL *ssl, int n)
{
    if(n > 0)
        return n;
    if(SSL_get_error(ssl, n) == SSL_ERROR_ZERO_RETURN)
        return 0;

    // Después de un fallo solo se permite liberar la conexión.
    SSL_set_shutdown(ssl, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
    return -1;
}

ssize_t conexion_leer(Conexion *conexion, void *datos, size_t largo)
{
    if(conexion->ssl) {
        if(SSL_get_shutdown(conexion->ssl) & SSL_SENT_SHUTDOWN)
            return -1;
        if(largo == 0)
            return 0;
        ERR_clear_error();
        int n = SSL_read(conexion->ssl, datos, largo > INT_MAX ? INT_MAX : (int)largo);
        return resultado_tls(conexion->ssl, n);
    }
    ssize_t n;
    do {
        n = recv(conexion->fd, datos, largo, 0);
    } while(n < 0 && errno == EINTR);
    return n;
}

ssize_t conexion_escribir(Conexion *conexion, const void *datos, size_t largo)
{
    if(conexion->ssl) {
        if(SSL_get_shutdown(conexion->ssl) & SSL_SENT_SHUTDOWN)
            return -1;
        if(largo == 0)
            return 0;
        ERR_clear_error();
        int n = SSL_write(conexion->ssl, datos, largo > INT_MAX ? INT_MAX : (int)largo);
        return resultado_tls(conexion->ssl, n);
    }
    ssize_t n;
    do {
        n = send(conexion->fd, datos, largo, MSG_NOSIGNAL);
    } while(n < 0 && errno == EINTR);
    return n;
}

void conexion_cerrar(Conexion *conexion)
{
    if(conexion->ssl) {
        int flags = fcntl(conexion->fd, F_GETFL);
        if(SSL_is_init_finished(conexion->ssl) &&
           !(SSL_get_shutdown(conexion->ssl) & SSL_SENT_SHUTDOWN) &&
           flags >= 0 && fcntl(conexion->fd, F_SETFL, flags | O_NONBLOCK) == 0) {
            ERR_clear_error();
            SSL_shutdown(conexion->ssl);
        }
        SSL_free(conexion->ssl);
        conexion->ssl = NULL;
    }
    if(conexion->fd >= 0) {
        close(conexion->fd);
        conexion->fd = -1;
    }
}
