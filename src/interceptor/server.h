#ifndef INTERCEPTOR_SERVER_H
#define INTERCEPTOR_SERVER_H

#include "config.h"

// Abre el socket UDP; el bucle solo termina si no puede continuar.
int server_run(const Config *config);

#endif
