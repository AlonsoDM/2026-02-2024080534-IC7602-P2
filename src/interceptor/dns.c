// Adaptado de src/interceptor/src/dns.rs del P1; operaciones según la documentación de ldns.
#include "dns.h"

#include <string.h>

ldns_status get_message(const uint8_t *packet, size_t length, ldns_pkt **message)
{
    *message = NULL;
    return ldns_wire2pkt(message, packet, length);
}

bool is_standard_query(const ldns_pkt *message)
{
    return !ldns_pkt_qr(message) && ldns_pkt_get_opcode(message) == LDNS_PACKET_QUERY;
}

const ldns_rr *first_question(const ldns_pkt *message)
{
    ldns_rr_list *questions = ldns_pkt_question(message);
    return ldns_rr_list_rr_count(questions) ? ldns_rr_list_rr(questions, 0) : NULL;
}

const ldns_rr *internal_question(const ldns_pkt *message)
{
    if(!is_standard_query(message) || ldns_pkt_qdcount(message) != 1)
        return NULL;
    const ldns_rr *question = first_question(message);
    if(!question || ldns_rr_get_type(question) != LDNS_RR_TYPE_A ||
       ldns_rr_get_class(question) != LDNS_RR_CLASS_IN)
        return NULL;
    return question;
}

static ldns_pkt *new_response(const ldns_pkt *request, ldns_pkt_rcode code)
{
    ldns_pkt *response = ldns_pkt_new();
    if(response) {
        ldns_pkt_set_id(response, ldns_pkt_id(request));
        ldns_pkt_set_qr(response, true);
        ldns_pkt_set_opcode(response, ldns_pkt_get_opcode(request));
        ldns_pkt_set_rd(response, ldns_pkt_rd(request));
        ldns_pkt_set_cd(response, ldns_pkt_cd(request));
        ldns_pkt_set_ra(response, true);
        ldns_pkt_set_rcode(response, code);
    }
    return response;
}

// push_rr toma la propiedad del registro cuando tiene éxito.
static bool copy_question(ldns_pkt *response, const ldns_rr *question)
{
    ldns_rr *copy = ldns_rr_clone(question);
    if(!copy)
        return false;
    if(!ldns_pkt_push_rr(response, LDNS_SECTION_QUESTION, copy)) {
        ldns_rr_free(copy);
        return false;
    }
    return true;
}

static ldns_status build_error_response(const ldns_pkt *request, ldns_pkt_rcode code,
                                        uint8_t **response, size_t *length)
{
    *response = NULL;
    *length = 0;
    ldns_pkt *message = new_response(request, code);
    if(!message)
        return LDNS_STATUS_MEM_ERR;
    ldns_rr_list *questions = ldns_pkt_question(request);
    for(size_t i = 0; i < ldns_rr_list_rr_count(questions); i++) {
        if(!copy_question(message, ldns_rr_list_rr(questions, i))) {
            ldns_pkt_free(message);
            return LDNS_STATUS_MEM_ERR;
        }
    }
    ldns_status status = ldns_pkt2wire(response, message, length);
    ldns_pkt_free(message);
    return status;
}

ldns_status build_internal_response(const ldns_pkt *request, const ldns_rr *question,
                                    struct in_addr address, uint32_t ttl,
                                    uint8_t **response, size_t *length)
{
    *response = NULL;
    *length = 0;
    ldns_pkt *message = new_response(request, LDNS_RCODE_NOERROR);
    if(!message)
        return LDNS_STATUS_MEM_ERR;
    ldns_pkt_set_aa(message, true);
    if(!copy_question(message, question)) {
        ldns_pkt_free(message);
        return LDNS_STATUS_MEM_ERR;
    }
    ldns_rr *answer = ldns_rr_new();
    ldns_rdf *owner = ldns_rdf_clone(ldns_rr_owner(question));
    ldns_rdf *data = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_A, sizeof address.s_addr, &address.s_addr);
    if(!answer || !owner || !data) {
        ldns_rr_free(answer);
        ldns_rdf_deep_free(owner);
        ldns_rdf_deep_free(data);
        ldns_pkt_free(message);
        return LDNS_STATUS_MEM_ERR;
    }
    ldns_rr_set_owner(answer, owner);
    ldns_rr_set_type(answer, LDNS_RR_TYPE_A);
    ldns_rr_set_class(answer, LDNS_RR_CLASS_IN);
    ldns_rr_set_ttl(answer, ttl);
    if(!ldns_rr_push_rdf(answer, data)) {
        ldns_rdf_deep_free(data);
        ldns_rr_free(answer);
        ldns_pkt_free(message);
        return LDNS_STATUS_MEM_ERR;
    }
    if(!ldns_pkt_push_rr(message, LDNS_SECTION_ANSWER, answer)) {
        ldns_rr_free(answer);
        ldns_pkt_free(message);
        return LDNS_STATUS_MEM_ERR;
    }
    ldns_status status = ldns_pkt2wire(response, message, length);
    ldns_pkt_free(message);
    return status;
}

ldns_status build_format_error_response(const uint8_t *packet, size_t packet_length,
                                        uint8_t **response, size_t *length)
{
    *response = NULL;
    *length = 0;
    if(packet_length < LDNS_HEADER_SIZE)
        return LDNS_STATUS_OK;
    // Solo se recupera el Header: se ponen los contadores en cero para que ldns lo lea.
    uint8_t header[LDNS_HEADER_SIZE];
    memcpy(header, packet, sizeof header);
    memset(header + 4, 0, sizeof header - 4);
    ldns_pkt *request = NULL;
    ldns_status status = get_message(header, sizeof header, &request);
    if(status == LDNS_STATUS_OK)
        status = build_error_response(request, LDNS_RCODE_FORMERR, response, length);
    ldns_pkt_free(request);
    return status;
}

ldns_status build_server_failure_response(const ldns_pkt *request,
                                          uint8_t **response, size_t *length)
{
    return build_error_response(request, LDNS_RCODE_SERVFAIL, response, length);
}

bool response_matches(const ldns_pkt *request, const uint8_t *packet, size_t length)
{
    ldns_pkt *response = NULL;
    if(get_message(packet, length, &response) != LDNS_STATUS_OK)
        return false;
    bool matches = ldns_pkt_qr(response) && ldns_pkt_id(response) == ldns_pkt_id(request) &&
        ldns_pkt_get_opcode(response) == ldns_pkt_get_opcode(request) &&
        ldns_pkt_qdcount(response) == ldns_pkt_qdcount(request);
    for(size_t i = 0; matches && i < ldns_pkt_qdcount(request); i++) {
        const ldns_rr *expected = ldns_rr_list_rr(ldns_pkt_question(request), i);
        const ldns_rr *actual = ldns_rr_list_rr(ldns_pkt_question(response), i);
        matches = ldns_rr_compare(expected, actual) == 0;
    }
    ldns_pkt_free(response);
    return matches;
}
