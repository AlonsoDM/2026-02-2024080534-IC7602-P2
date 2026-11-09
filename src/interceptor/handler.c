// Mismo flujo de handler.rs del P1, con resolve del P2 y reenvío UDP directo.
#include "handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "dns.h"
#include "upstream.h"

static int resolve_message(const uint8_t *packet, size_t packet_length,
                           const struct sockaddr_in *sender, const Config *config,
                           const ldns_pkt *message, uint8_t **response, size_t *length)
{
    const ldns_rr *question = internal_question(message);
    if(!question)
        return resolve_external(config, packet, packet_length, message, response, length);

    char *domain = ldns_rdf2str(ldns_rr_owner(question));
    if(!domain)
        return -1;
    size_t domain_length = strlen(domain);
    if(domain_length && domain[domain_length - 1] == '.')
        domain[domain_length - 1] = '\0';
    ResolveResult result;
    int status = resolve_internal(config, domain, sender->sin_addr, &result);
    free(domain);
    if(status != 0)
        return -1;
    if(!result.exists)
        return resolve_external(config, packet, packet_length, message, response, length);
    return build_internal_response(message, question, result.address, config->dns_ttl_seconds,
                                   response, length) == LDNS_STATUS_OK ? 0 : -1;
}

int process_packet(const uint8_t *packet, size_t packet_length,
                   const struct sockaddr_in *sender, const Config *config,
                   uint8_t **response, size_t *length)
{
    *response = NULL;
    *length = 0;
    ldns_pkt *message = NULL;
    ldns_status parsed = get_message(packet, packet_length, &message);
    if(parsed == LDNS_STATUS_MEM_ERR)
        return -1;
    if(parsed != LDNS_STATUS_OK)
        return build_format_error_response(packet, packet_length, response, length) ==
            LDNS_STATUS_OK ? 0 : -1;

    int status = resolve_message(packet, packet_length, sender, config, message, response, length);
    if(status != 0) {
        free(*response);
        *response = NULL;
        *length = 0;
        status = build_server_failure_response(message, response, length) == LDNS_STATUS_OK ? 0 : -1;
    }
    ldns_pkt_free(message);
    return status;
}
