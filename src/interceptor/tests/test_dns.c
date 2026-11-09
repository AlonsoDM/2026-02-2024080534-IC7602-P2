#include <arpa/inet.h>
#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "dns.h"
#include "fixtures.h"

static ldns_pkt *query(void)
{
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY, &message), LDNS_STATUS_OK);
    return message;
}

Test(dns, reads_header_and_question_from_valid_packet)
{
    ldns_pkt *message = query();
    cr_assert_eq(ldns_pkt_id(message), 0x1234);
    cr_assert(is_standard_query(message));
    const ldns_rr *question = first_question(message);
    cr_assert_not_null(question);
    char *name = ldns_rdf2str(ldns_rr_owner(question));
    cr_assert_str_eq(name, "example.com.");
    cr_assert_eq(ldns_rr_get_type(question), LDNS_RR_TYPE_A);
    cr_assert_eq(ldns_rr_get_class(question), LDNS_RR_CLASS_IN);
    free(name);
    ldns_pkt_free(message);
}

Test(dns, rejects_malformed_packets_and_compression_loops)
{
    uint8_t packet[sizeof STANDARD_A_QUERY];
    memcpy(packet, STANDARD_A_QUERY, sizeof packet);
    packet[12] = 0xc0;
    packet[13] = 12;
    ldns_pkt *message = NULL;
    cr_assert_neq(get_message(packet, sizeof packet, &message), LDNS_STATUS_OK);
    cr_assert_null(message);
    packet[13] = 255;
    cr_assert_neq(get_message(packet, sizeof packet, &message), LDNS_STATUS_OK);
    cr_assert_neq(get_message(STANDARD_A_QUERY, 2, &message), LDNS_STATUS_OK);
    cr_assert_neq(get_message(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY - 1, &message), LDNS_STATUS_OK);
}

Test(dns, accepts_compressed_question)
{
    // Dos preguntas: la segunda usa un puntero al nombre de la primera.
    uint8_t packet[sizeof STANDARD_A_QUERY + 6];
    memcpy(packet, STANDARD_A_QUERY, sizeof STANDARD_A_QUERY);
    packet[5] = 2;
    const uint8_t second[] = {0xc0, 0x0c, 0, 1, 0, 1};
    memcpy(packet + sizeof STANDARD_A_QUERY, second, sizeof second);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(packet, sizeof packet, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_qdcount(message), 2);
    cr_assert_null(internal_question(message));
    cr_assert_eq(ldns_rr_compare(first_question(message),
        ldns_rr_list_rr(ldns_pkt_question(message), 1)), 0);
    ldns_pkt_free(message);
}

Test(dns, only_one_standard_a_in_question_is_internal)
{
    ldns_pkt *message = query();
    cr_assert_not_null(internal_question(message));
    ldns_pkt_set_qr(message, true);
    cr_assert(!is_standard_query(message));
    cr_assert_null(internal_question(message));
    ldns_pkt_set_qr(message, false);
    ldns_pkt_set_opcode(message, LDNS_PACKET_STATUS);
    cr_assert_null(internal_question(message));
    ldns_pkt_set_opcode(message, LDNS_PACKET_QUERY);
    ldns_rr *question = ldns_rr_list_rr(ldns_pkt_question(message), 0);
    ldns_rr_set_type(question, LDNS_RR_TYPE_AAAA);
    cr_assert_null(internal_question(message));
    ldns_rr_set_type(question, LDNS_RR_TYPE_A);
    ldns_rr_set_class(question, LDNS_RR_CLASS_CH);
    cr_assert_null(internal_question(message));
    ldns_pkt_free(message);
}

Test(dns, handles_message_without_questions)
{
    uint8_t packet[LDNS_HEADER_SIZE] = {0};
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(packet, sizeof packet, &message), LDNS_STATUS_OK);
    cr_assert_null(first_question(message));
    cr_assert_null(internal_question(message));
    ldns_pkt_free(message);
}

Test(dns, builds_internal_a_response)
{
    ldns_pkt *request = query(), *message = NULL;
    ldns_pkt_set_cd(request, true);
    struct in_addr address;
    cr_assert_eq(inet_pton(AF_INET, "192.0.2.10", &address), 1);
    uint8_t *packet = NULL;
    size_t length;
    cr_assert_eq(build_internal_response(request, internal_question(request), address, 60,
        &packet, &length), LDNS_STATUS_OK);
    cr_assert_eq(get_message(packet, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_id(message), 0x1234);
    cr_assert(ldns_pkt_qr(message));
    cr_assert(ldns_pkt_aa(message));
    cr_assert(ldns_pkt_rd(message));
    cr_assert(ldns_pkt_ra(message));
    cr_assert(ldns_pkt_cd(message));
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_NOERROR);
    cr_assert_eq(ldns_pkt_qdcount(message), 1);
    cr_assert_eq(ldns_pkt_ancount(message), 1);
    cr_assert_eq(ldns_pkt_nscount(message), 0);
    cr_assert_eq(ldns_pkt_arcount(message), 0);
    cr_assert_eq(ldns_rr_compare(first_question(request), first_question(message)), 0);
    const ldns_rr *answer = ldns_rr_list_rr(ldns_pkt_answer(message), 0);
    cr_assert_eq(ldns_rr_get_type(answer), LDNS_RR_TYPE_A);
    cr_assert_eq(ldns_rr_get_class(answer), LDNS_RR_CLASS_IN);
    cr_assert_eq(ldns_rr_ttl(answer), 60);
    cr_assert_eq(ldns_rdf_size(ldns_rr_rdf(answer, 0)), sizeof address.s_addr);
    cr_assert_eq(memcmp(ldns_rdf_data(ldns_rr_rdf(answer, 0)), &address.s_addr, sizeof address.s_addr), 0);
    cr_assert(response_matches(request, packet, length));
    packet[0] ^= 1;
    cr_assert(!response_matches(request, packet, length));
    free(packet);
    ldns_pkt_free(message);
    ldns_pkt_free(request);
}

Test(dns, builds_format_error_when_header_is_available)
{
    uint8_t *packet = NULL;
    size_t length;
    cr_assert_eq(build_format_error_response(STANDARD_A_QUERY, LDNS_HEADER_SIZE,
        &packet, &length), LDNS_STATUS_OK);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(packet, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_id(message), 0x1234);
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_FORMERR);
    cr_assert(ldns_pkt_qr(message));
    cr_assert(ldns_pkt_rd(message));
    cr_assert_eq(ldns_pkt_qdcount(message), 0);
    cr_assert_eq(ldns_pkt_ancount(message), 0);
    ldns_pkt_free(message);
    free(packet);
}

Test(dns, omits_format_error_when_header_is_unavailable)
{
    uint8_t *packet = NULL;
    size_t length = 123;
    cr_assert_eq(build_format_error_response(STANDARD_A_QUERY, 2, &packet, &length), LDNS_STATUS_OK);
    cr_assert_null(packet);
    cr_assert_eq(length, 0);
}

Test(dns, builds_server_failure_from_valid_request)
{
    ldns_pkt *request = query(), *message = NULL;
    uint8_t *packet = NULL;
    size_t length;
    cr_assert_eq(build_server_failure_response(request, &packet, &length), LDNS_STATUS_OK);
    cr_assert_eq(get_message(packet, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_id(message), 0x1234);
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_SERVFAIL);
    cr_assert_eq(ldns_rr_compare(first_question(request), first_question(message)), 0);
    cr_assert_eq(ldns_pkt_ancount(message), 0);
    free(packet);
    ldns_pkt_free(request);
    ldns_pkt_free(message);
}
