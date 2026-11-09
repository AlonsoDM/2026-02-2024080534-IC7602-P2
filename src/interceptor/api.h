#ifndef INTERCEPTOR_API_H
#define INTERCEPTOR_API_H

#include "config.h"
#include <stdbool.h>

typedef struct {
    bool exists;
    struct in_addr address;
} ResolveResult;

// 0 con el resultado de resolve, -1 ante error HTTP, de comunicación o de JSON.
int resolve_internal(const Config *config, const char *domain,
                     struct in_addr client_ip, ResolveResult *result);

#endif
