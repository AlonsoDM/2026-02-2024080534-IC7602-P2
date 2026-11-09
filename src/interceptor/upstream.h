#ifndef INTERCEPTOR_UPSTREAM_H
#define INTERCEPTOR_UPSTREAM_H

#include "config.h"
#include "dns.h"

// 0 con un response que se libera con free, -1 ante error o timeout.
int resolve_external(const Config *config, const uint8_t *packet, size_t packet_length,
                     const ldns_pkt *request, uint8_t **response, size_t *length);

#endif
