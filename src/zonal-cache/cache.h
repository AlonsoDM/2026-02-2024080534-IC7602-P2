#ifndef CACHE_H
#define CACHE_H

#include <time.h>

#include "config.h"
#include "http.h"

int cache_iniciar(const char *directorio, int segundos_purga);
int cache_buscar(const UrlConfig *url, const Peticion *p, Respuesta *salida);
void cache_guardar(const UrlConfig *url, const Peticion *p, const Respuesta *r);
void cache_purgar(void);
void cache_adelantar_reloj(time_t segundos);

#endif
