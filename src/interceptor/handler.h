#ifndef INTERCEPTOR_HANDLER_H
#define INTERCEPTOR_HANDLER_H

#include "config.h"
#include <stddef.h>
#include <stdint.h>

// 0 con response (o NULL si no se debe responder), -1 si no se pudo construir.
// El llamador libera el response con free.
int process_packet(const uint8_t *packet, size_t packet_length,
                   const struct sockaddr_in *sender, const Config *config,
                   uint8_t **response, size_t *length);

#endif
