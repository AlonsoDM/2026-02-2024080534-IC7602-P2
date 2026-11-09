#ifndef AUTH_H
#define AUTH_H

#include "config.h"
#include "http.h"

void auth_iniciar(const ConfigCache *config);
int auth_verificar(const UrlConfig *url, const Peticion *p, Respuesta *salida);

#endif
