#include <arpa/inet.h>
#include <criterion/criterion.h>
#include <curl/curl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "api.h"
#include "dns.h"
#include "fixtures.h"
#include "handler.h"
#include "server.h"
#include "upstream.h"

static int bind_local(int type, struct sockaddr_in *address)
{
    int fd = socket(AF_INET, type, 0);
    cr_assert_geq(fd, 0);
    *address = (struct sockaddr_in){.sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    cr_assert_eq(bind(fd, (struct sockaddr *)address, sizeof *address), 0);
    socklen_t length = sizeof *address;
    cr_assert_eq(getsockname(fd, (struct sockaddr *)address, &length), 0);
    struct timeval timeout = {.tv_sec = 4};
    cr_assert_eq(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout), 0);
    return fd;
}

static Config local_config(void)
{
    Config config = {.listen_port = 5300, .upstream_timeout_seconds = 1,
        .dns_ttl_seconds = 60, .node_api_key = "test-interceptor-key", .node_api_timeout_seconds = 2};
    config.upstream_address = (struct sockaddr_in){.sin_family = AF_INET,
        .sin_port = htons(9), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    return config;
}

typedef struct {
    int fd;
    int status;
    const char *body;
    char request[4096];
    char url[80];
    pthread_t thread;
} FakeApi;

static void *serve_api(void *context)
{
    FakeApi *api = context;
    struct pollfd ready = {.fd = api->fd, .events = POLLIN};
    if(poll(&ready, 1, 4000) <= 0)
        return NULL;
    int client = accept(api->fd, NULL, NULL);
    if(client < 0)
        return NULL;
    struct timeval timeout = {.tv_sec = 4};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    size_t used = 0;
    while(used < sizeof api->request - 1) {
        ssize_t n = recv(client, api->request + used, sizeof api->request - used - 1, 0);
        if(n <= 0)
            break;
        used += (size_t)n;
        api->request[used] = '\0';
        if(strstr(api->request, "\r\n\r\n"))
            break;
    }
    char header[200];
    int n = snprintf(header, sizeof header,
        "HTTP/1.1 %d Test\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        api->status, strlen(api->body));
    send(client, header, (size_t)n, MSG_NOSIGNAL);
    size_t remaining = strlen(api->body);
    const char *body = api->body;
    while(remaining) {
        ssize_t sent = send(client, body, remaining, MSG_NOSIGNAL);
        if(sent <= 0)
            break;
        remaining -= (size_t)sent;
        body += sent;
    }
    close(client);
    return NULL;
}

static void start_api(FakeApi *api, Config *config, int status, const char *body)
{
    *api = (FakeApi){.status = status, .body = body};
    struct sockaddr_in address;
    api->fd = bind_local(SOCK_STREAM, &address);
    cr_assert_eq(listen(api->fd, 8), 0);
    snprintf(api->url, sizeof api->url, "http://127.0.0.1:%u/", ntohs(address.sin_port));
    config->node_api_url = api->url;
    cr_assert_eq(pthread_create(&api->thread, NULL, serve_api, api), 0);
}

static void stop_api(FakeApi *api)
{
    cr_assert_eq(pthread_join(api->thread, NULL), 0);
    close(api->fd);
}

typedef struct {
    int fd;
    int count;
    bool wrong_first;
    bool same_packets;
    uint8_t packets[2][512];
    size_t lengths[2];
    pthread_t thread;
} FakeDns;

static void *serve_dns(void *context)
{
    FakeDns *dns = context;
    struct sockaddr_in senders[2];
    dns->same_packets = true;
    // Esperar ambas consultas antes de responder permite detectar un servidor serial.
    for(int i = 0; i < dns->count; i++) {
        socklen_t length = sizeof senders[i];
        ssize_t received = recvfrom(dns->fd, dns->packets[i], sizeof dns->packets[i], 0,
            (struct sockaddr *)&senders[i], &length);
        if(received <= 0) {
            dns->same_packets = false;
            return NULL;
        }
        dns->lengths[i] = (size_t)received;
    }
    for(int i = dns->count - 1; i >= 0; i--) {
        uint8_t response[512];
        memcpy(response, dns->packets[i], dns->lengths[i]);
        response[2] |= 0x80;
        if(dns->wrong_first) {
            response[0] ^= 1;
            sendto(dns->fd, response, dns->lengths[i], 0,
                (struct sockaddr *)&senders[i], sizeof senders[i]);
            response[0] ^= 1;
            // Identificador correcto pero otra pregunta: tampoco debe aceptarse.
            response[13] = 'z';
            sendto(dns->fd, response, dns->lengths[i], 0,
                (struct sockaddr *)&senders[i], sizeof senders[i]);
            response[13] = dns->packets[i][13];
        }
        sendto(dns->fd, response, dns->lengths[i], 0,
            (struct sockaddr *)&senders[i], sizeof senders[i]);
    }
    return NULL;
}

static void start_dns(FakeDns *dns, Config *config, int count, bool wrong_first)
{
    *dns = (FakeDns){.count = count, .wrong_first = wrong_first};
    dns->fd = bind_local(SOCK_DGRAM, &config->upstream_address);
    cr_assert_eq(pthread_create(&dns->thread, NULL, serve_dns, dns), 0);
}

static void stop_dns(FakeDns *dns)
{
    cr_assert_eq(pthread_join(dns->thread, NULL), 0);
    close(dns->fd);
    cr_assert(dns->same_packets);
}

static void setup(void)
{
    cr_assert_eq(curl_global_init(CURL_GLOBAL_DEFAULT), CURLE_OK);
    // Las pruebas locales nunca pasan por un proxy del equipo.
    setenv("NO_PROXY", "127.0.0.1,localhost", 1);
}

static void cleanup(void)
{
    curl_global_cleanup();
}

TestSuite(network, .init = setup, .fini = cleanup, .timeout = 12);

Test(network, api_sends_domain_client_ip_key_and_parses_positive_result)
{
    Config config = local_config();
    FakeApi api;
    start_api(&api, &config, 200, "{\"exists\":true,\"zone\":\"GB\",\"address\":\"203.0.113.24\"}");
    struct in_addr client_ip;
    inet_pton(AF_INET, "81.2.69.160", &client_ip);
    ResolveResult result;
    cr_assert_eq(resolve_internal(&config, "example.com", client_ip, &result), 0);
    cr_assert(result.exists);
    char ip[INET_ADDRSTRLEN];
    cr_assert_not_null(inet_ntop(AF_INET, &result.address, ip, sizeof ip));
    cr_assert_str_eq(ip, "203.0.113.24");
    stop_api(&api);
    cr_assert_not_null(strstr(api.request,
        "GET /api/interceptor/resolve?domain=example.com&client_ip=81.2.69.160 HTTP/1.1\r\n"));
    cr_assert_not_null(strstr(api.request, "x-api-key: test-interceptor-key\r\n"));
    cr_assert_null(strstr(api.request, "/api/exists"));
}

Test(network, api_rejects_http_errors_invalid_json_and_incomplete_contract)
{
    const char *bodies[] = {"not-json", "{}", "{\"exists\":\"false\"}",
        "{\"exists\":true}", "{\"exists\":true,\"zone\":\"GB\",\"address\":\"bad-ip\"}",
        "{\"exists\":false} trailing"};
    for(size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        Config config = local_config();
        FakeApi api;
        start_api(&api, &config, 200, bodies[i]);
        ResolveResult result;
        cr_assert_neq(resolve_internal(&config, "example.com", config.upstream_address.sin_addr, &result), 0);
        stop_api(&api);
    }
    const int codes[] = {302, 401, 403, 404, 500};
    for(size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        Config config = local_config();
        FakeApi api;
        start_api(&api, &config, codes[i], "{\"exists\":false}");
        ResolveResult result;
        cr_assert_neq(resolve_internal(&config, "example.com", config.upstream_address.sin_addr, &result), 0);
        stop_api(&api);
    }
    // La URL se interpreta en api.c, no en el cargador de configuración.
    const char *urls[] = {"file:///etc/passwd", "ftp://example.test", "http://", "invalid"};
    for(size_t i = 0; i < sizeof urls / sizeof urls[0]; i++) {
        Config config = local_config();
        config.node_api_url = urls[i];
        ResolveResult result;
        cr_assert_neq(resolve_internal(&config, "example.com", config.upstream_address.sin_addr, &result), 0);
    }
}

Test(network, api_bounds_response_size)
{
    char body[20000];
    memset(body, ' ', sizeof body - 1);
    body[sizeof body - 1] = '\0';
    Config config = local_config();
    FakeApi api;
    start_api(&api, &config, 200, body);
    ResolveResult result;
    cr_assert_neq(resolve_internal(&config, "example.com", config.upstream_address.sin_addr, &result), 0);
    stop_api(&api);
}

Test(network, upstream_preserves_packets_and_ignores_wrong_responses)
{
    Config config = local_config();
    FakeDns dns;
    start_dns(&dns, &config, 1, true);
    ldns_pkt *request = NULL;
    cr_assert_eq(get_message(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY, &request), LDNS_STATUS_OK);
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(resolve_external(&config, STANDARD_A_QUERY, sizeof STANDARD_A_QUERY,
        request, &response, &length), 0);
    cr_assert_eq(length, sizeof STANDARD_A_QUERY);
    uint8_t expected[sizeof STANDARD_A_QUERY];
    memcpy(expected, STANDARD_A_QUERY, sizeof expected);
    expected[2] |= 0x80;
    cr_assert_eq(memcmp(response, expected, length), 0);
    stop_dns(&dns);
    cr_assert_eq(dns.lengths[0], sizeof STANDARD_A_QUERY);
    cr_assert_eq(memcmp(dns.packets[0], STANDARD_A_QUERY, sizeof STANDARD_A_QUERY), 0);
    free(response);
    ldns_pkt_free(request);
}

Test(network, handler_returns_selected_cache_without_contacting_upstream)
{
    Config config = local_config();
    FakeApi api;
    start_api(&api, &config, 200, "{\"exists\":true,\"zone\":\"GB\",\"address\":\"203.0.113.24\"}");
    struct sockaddr_in sender = {.sin_family = AF_INET};
    inet_pton(AF_INET, "81.2.69.160", &sender.sin_addr);
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(process_packet(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY, &sender, &config,
        &response, &length), 0);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(response, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_ancount(message), 1);
    const ldns_rr *answer = ldns_rr_list_rr(ldns_pkt_answer(message), 0);
    cr_assert_eq(ldns_rr_ttl(answer), config.dns_ttl_seconds);
    struct in_addr expected;
    inet_pton(AF_INET, "203.0.113.24", &expected);
    cr_assert_eq(memcmp(ldns_rdf_data(ldns_rr_rdf(answer, 0)), &expected, sizeof expected), 0);
    stop_api(&api);
    free(response);
    ldns_pkt_free(message);
}

Test(network, handler_uses_upstream_only_when_api_reports_absent_domain)
{
    Config config = local_config();
    FakeApi api;
    FakeDns dns;
    start_api(&api, &config, 200, "{\"exists\":false}");
    start_dns(&dns, &config, 1, false);
    struct sockaddr_in sender = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(process_packet(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY, &sender, &config,
        &response, &length), 0);
    cr_assert_eq(length, sizeof STANDARD_A_QUERY);
    cr_assert_eq(response[2], STANDARD_A_QUERY[2] | 0x80);
    stop_api(&api);
    stop_dns(&dns);
    free(response);
}

Test(network, handler_returns_servfail_for_api_errors_without_upstream_fallback)
{
    Config config = local_config();
    FakeApi api;
    start_api(&api, &config, 404, "{\"exists\":false}");
    // Si el handler reenvía por error, encontrará un response exitoso en este socket.
    int upstream = bind_local(SOCK_DGRAM, &config.upstream_address);
    struct sockaddr_in sender = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(process_packet(STANDARD_A_QUERY, sizeof STANDARD_A_QUERY, &sender, &config,
        &response, &length), 0);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(response, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_SERVFAIL);
    struct pollfd ready = {.fd = upstream, .events = POLLIN};
    cr_assert_eq(poll(&ready, 1, 0), 0);
    close(upstream);
    stop_api(&api);
    free(response);
    ldns_pkt_free(message);
}

Test(network, handler_returns_servfail_when_upstream_times_out)
{
    Config config = local_config();
    int upstream = bind_local(SOCK_DGRAM, &config.upstream_address);
    uint8_t packet[sizeof STANDARD_A_QUERY];
    memcpy(packet, STANDARD_A_QUERY, sizeof packet);
    packet[sizeof packet - 3] = LDNS_RR_TYPE_AAAA;
    struct sockaddr_in sender = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(process_packet(packet, sizeof packet, &sender, &config, &response, &length), 0);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(response, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_SERVFAIL);
    close(upstream);
    free(response);
    ldns_pkt_free(message);
}

Test(network, handler_does_not_forward_malformed_packets)
{
    Config config = local_config();
    struct sockaddr_in sender = {.sin_family = AF_INET};
    uint8_t *response = NULL;
    size_t length;
    cr_assert_eq(process_packet(STANDARD_A_QUERY, 2, &sender, &config, &response, &length), 0);
    cr_assert_null(response);
    cr_assert_eq(process_packet(STANDARD_A_QUERY, LDNS_HEADER_SIZE, &sender, &config,
        &response, &length), 0);
    ldns_pkt *message = NULL;
    cr_assert_eq(get_message(response, length, &message), LDNS_STATUS_OK);
    cr_assert_eq(ldns_pkt_get_rcode(message), LDNS_RCODE_FORMERR);
    free(response);
    ldns_pkt_free(message);
}

Test(network, server_processes_two_packets_concurrently_without_mixing_responses)
{
    Config config = local_config();
    config.upstream_timeout_seconds = 3;
    struct sockaddr_in address;
    int reserved = bind_local(SOCK_DGRAM, &address);
    config.listen_port = ntohs(address.sin_port);
    close(reserved);
    FakeDns dns;
    // Se crea el servidor antes que los hilos del DNS falso.
    dns = (FakeDns){.count = 2};
    dns.fd = bind_local(SOCK_DGRAM, &config.upstream_address);
    pid_t child = fork();
    cr_assert_geq(child, 0);
    if(child == 0)
        _exit(server_run(&config) == 0 ? 0 : 1);
    cr_assert_eq(pthread_create(&dns.thread, NULL, serve_dns, &dns), 0);
    int client = socket(AF_INET, SOCK_DGRAM, 0);
    cr_assert_geq(client, 0);
    cr_assert_eq(connect(client, (struct sockaddr *)&address, sizeof address), 0);
    uint8_t packets[2][sizeof STANDARD_A_QUERY];
    for(int i = 0; i < 2; i++) {
        memcpy(packets[i], STANDARD_A_QUERY, sizeof STANDARD_A_QUERY);
        packets[i][1] = (uint8_t)(0x34 + i);
        packets[i][sizeof STANDARD_A_QUERY - 3] = LDNS_RR_TYPE_AAAA;
    }
    // Esperar que el servidor abra su puerto, sin mandar consultas al upstream.
    bool started = false;
    for(int i = 0; i < 100; i++) {
        send(client, STANDARD_A_QUERY, LDNS_HEADER_SIZE, 0);
        struct pollfd ready = {.fd = client, .events = POLLIN};
        if(poll(&ready, 1, 20) > 0) {
            uint8_t response[512];
            ssize_t n = recv(client, response, sizeof response, 0);
            if(n >= LDNS_HEADER_SIZE && (response[3] & 0x0f) == LDNS_RCODE_FORMERR) {
                started = true;
                break;
            }
        }
    }
    if(!started) {
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
        cr_assert_fail("El servidor no abrió el puerto UDP");
    }
    for(int i = 0; i < 2; i++)
        cr_assert_eq(send(client, packets[i], sizeof packets[i], 0), (ssize_t)sizeof packets[i]);
    bool seen[2] = {false, false};
    bool valid = true;
    for(int i = 0; i < 2; i++) {
        struct pollfd ready = {.fd = client, .events = POLLIN};
        if(poll(&ready, 1, 4000) <= 0) {
            valid = false;
            break;
        }
        uint8_t response[512];
        ssize_t n = recv(client, response, sizeof response, 0);
        if(n != (ssize_t)sizeof STANDARD_A_QUERY || response[1] < 0x34 || response[1] > 0x35) {
            valid = false;
            break;
        }
        int index = response[1] - 0x34;
        uint8_t expected[sizeof STANDARD_A_QUERY];
        memcpy(expected, packets[index], sizeof expected);
        expected[2] |= 0x80;
        valid = valid && !seen[index] && memcmp(response, expected, sizeof expected) == 0;
        seen[index] = true;
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    close(client);
    stop_dns(&dns);
    cr_assert(valid && seen[0] && seen[1]);
    for(int i = 0; i < 2; i++) {
        cr_assert_eq(dns.lengths[i], sizeof STANDARD_A_QUERY);
        cr_assert_eq(dns.packets[i][sizeof STANDARD_A_QUERY - 3], LDNS_RR_TYPE_AAAA);
    }
}
