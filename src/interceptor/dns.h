#ifndef INTERCEPTOR_DNS_H
#define INTERCEPTOR_DNS_H

#include <ldns/ldns.h>
#include <netinet/in.h>
#include <stdbool.h>

// El mensaje pertenece al llamador, que lo libera con ldns_pkt_free.
ldns_status get_message(const uint8_t *packet, size_t length, ldns_pkt **message);
bool is_standard_query(const ldns_pkt *message);
// Referencias al mensaje: no se liberan por separado.
const ldns_rr *first_question(const ldns_pkt *message);
const ldns_rr *internal_question(const ldns_pkt *message);

// Los responses pertenecen al llamador, que los libera con free.
ldns_status build_internal_response(const ldns_pkt *request, const ldns_rr *question,
                                    struct in_addr address, uint32_t ttl,
                                    uint8_t **response, size_t *length);
// OK con response NULL significa que no se pudo recuperar el Header.
ldns_status build_format_error_response(const uint8_t *packet, size_t packet_length,
                                        uint8_t **response, size_t *length);
ldns_status build_server_failure_response(const ldns_pkt *request,
                                          uint8_t **response, size_t *length);
// Revisa el response externo sin modificar el paquete.
bool response_matches(const ldns_pkt *request, const uint8_t *packet, size_t length);

#endif
