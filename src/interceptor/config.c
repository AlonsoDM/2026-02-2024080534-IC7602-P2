#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *read_variable(const char *name)
{
    const char *value = getenv(name);
    if(!value || !value[0]) {
        fprintf(stderr, "Falta la variable de entorno %s\n", name);
        return NULL;
    }
    return value;
}

static int read_number(const char *name, unsigned long long minimum,
                       unsigned long long maximum, unsigned long long *value)
{
    const char *raw = read_variable(name);
    if(!raw)
        return -1;
    char *end;
    errno = 0;
    unsigned long long parsed = strtoull(raw, &end, 10);
    if(raw[0] < '0' || raw[0] > '9' || errno || *end ||
       parsed < minimum || parsed > maximum) {
        fprintf(stderr, "%s debe ser un entero entre %llu y %llu\n", name, minimum, maximum);
        return -1;
    }
    *value = parsed;
    return 0;
}

int config_from_env(Config *config)
{
    *config = (Config){0};
    unsigned long long port, upstream_port, timeout, ttl, api_timeout;
    if(read_number("DNS_PORT", 1, UINT16_MAX, &port) != 0 ||
       read_number("UPSTREAM_DNS_PORT", 1, UINT16_MAX, &upstream_port) != 0 ||
       read_number("UPSTREAM_TIMEOUT_SECONDS", 1, INT_MAX / 1000, &timeout) != 0 ||
       read_number("DNS_TTL_SECONDS", 0, UINT32_MAX, &ttl) != 0 ||
       read_number("NODE_API_TIMEOUT_SECONDS", 1, INT_MAX / 1000, &api_timeout) != 0)
        return -1;

    const char *upstream = read_variable("UPSTREAM_DNS");
    const char *api_url = read_variable("NODE_API_URL");
    const char *api_key = read_variable("NODE_API_KEY");
    if(!upstream || !api_url || !api_key)
        return -1;
    if(inet_pton(AF_INET, upstream, &config->upstream_address.sin_addr) != 1) {
        fprintf(stderr, "UPSTREAM_DNS debe ser una dirección IPv4 válida\n");
        return -1;
    }
    // La llave va en un header; no se permiten saltos de línea.
    if(strpbrk(api_key, "\r\n")) {
        fprintf(stderr, "NODE_API_KEY no puede contener saltos de línea\n");
        return -1;
    }
    config->listen_port = (uint16_t)port;
    config->upstream_address.sin_family = AF_INET;
    config->upstream_address.sin_port = htons((uint16_t)upstream_port);
    config->upstream_timeout_seconds = (int)timeout;
    config->dns_ttl_seconds = (uint32_t)ttl;
    config->node_api_url = api_url;
    config->node_api_key = api_key;
    config->node_api_timeout_seconds = (int)api_timeout;
    return 0;
}
