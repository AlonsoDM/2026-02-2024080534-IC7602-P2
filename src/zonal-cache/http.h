#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

#include "conexion.h"

#define HTTP_MAX_CABECERA 16384
#define HTTP_MAX_HEADERS 64
#define HTTP_MAX_CUERPO (8 * 1024 * 1024)

typedef struct {
    const char *nombre;
    const char *valor;
} Header;

// Todo apunta adentro de crudo menos el cuerpo
typedef struct {
    char crudo[HTTP_MAX_CABECERA + 1];
    const char *metodo;
    const char *ruta;
    const char *host;
    Header headers[HTTP_MAX_HEADERS];
    size_t num_headers;
    char *cuerpo;
    size_t largo_cuerpo;
    bool tls;
} Peticion;

// headers son líneas "Nombre: valor\r\n" ya armadas
typedef struct {
    int codigo;
    char *headers;
    char *cuerpo;
    size_t largo_cuerpo;
} Respuesta;

// Texto que crece por pedazos sin pasarse de limite
typedef struct {
    char *datos;
    size_t largo;
    size_t limite;
} Buffer;

int http_leer_peticion(Conexion *conexion, Peticion *p);
const char *http_header(const Peticion *p, const char *nombre);
void http_liberar_peticion(Peticion *p);

void http_respuesta_texto(Respuesta *r, int codigo, const char *texto);
int http_enviar_respuesta(Conexion *conexion, const Respuesta *r);
void http_liberar_respuesta(Respuesta *r);

size_t http_acumular(char *pedazo, size_t tamano, size_t cuantos, void *destino);
int http_unir_url(char *destino, size_t tam, const char *base, const char *ruta);

#endif
