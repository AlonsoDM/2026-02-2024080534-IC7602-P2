#ifndef INTERCEPTOR_CONFIG_H
#define INTERCEPTOR_CONFIG_H

#include <netinet/in.h>
#include <stdint.h>

typedef struct {
    uint16_t listen_port;
    struct sockaddr_in upstream_address;
    int upstream_timeout_seconds;
    uint32_t dns_ttl_seconds;
    const char *node_api_url;
    const char *node_api_key;
    int node_api_timeout_seconds;
} Config;

// Los textos apuntan al ENV y se usan como solo lectura durante la ejecución.
int config_from_env(Config *config);

#endif
