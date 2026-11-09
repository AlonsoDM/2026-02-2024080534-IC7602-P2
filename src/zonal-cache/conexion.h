#ifndef CONEXION_H
#define CONEXION_H

#include <stddef.h>
#include <sys/types.h>
#include <openssl/ssl.h>

typedef struct {
    int fd;
    SSL *ssl; // NULL en HTTP; pertenece a esta conexión
} Conexion;

ssize_t conexion_leer(Conexion *conexion, void *datos, size_t largo);
ssize_t conexion_escribir(Conexion *conexion, const void *datos, size_t largo);
void conexion_cerrar(Conexion *conexion);

#endif
