#ifndef ORIGEN_H
#define ORIGEN_H

#include "config.h"
#include "http.h"

int origen_traer(const UrlConfig *url, const Peticion *p, Respuesta *salida);

#endif
