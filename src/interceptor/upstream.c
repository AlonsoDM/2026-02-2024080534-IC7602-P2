#include "upstream.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long long milliseconds(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int resolve_external(const Config *config, const uint8_t *packet, size_t packet_length,
                     const ldns_pkt *request, uint8_t **response, size_t *length)
{
    *response = NULL;
    *length = 0;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0)
        return -1;
    // Un socket por consulta. connect limita los responses al IP y puerto del DNS externo.
    if(connect(socket_fd, (const struct sockaddr *)&config->upstream_address,
               sizeof config->upstream_address) != 0 ||
       send(socket_fd, packet, packet_length, 0) != (ssize_t)packet_length) {
        close(socket_fd);
        return -1;
    }
    uint8_t *buffer = malloc(LDNS_MAX_PACKETLEN);
    long long start = milliseconds();
    int result = -1;
    if(buffer && start >= 0) {
        long long deadline = start + (long long)config->upstream_timeout_seconds * 1000;
        for(;;) {
            long long now = milliseconds();
            if(now < 0 || now >= deadline)
                break;
            struct pollfd wait = {.fd = socket_fd, .events = POLLIN};
            int ready = poll(&wait, 1, (int)(deadline - now));
            if(ready < 0 && errno == EINTR)
                continue;
            if(ready <= 0 || !(wait.revents & POLLIN))
                break;
            ssize_t received = recv(socket_fd, buffer, LDNS_MAX_PACKETLEN, 0);
            if(received < 0) {
                if(errno == EINTR)
                    continue;
                break;
            }
            if(!response_matches(request, buffer, (size_t)received))
                continue;
            *response = buffer;
            *length = (size_t)received;
            result = 0;
            break;
        }
    }
    if(result != 0)
        free(buffer);
    close(socket_fd);
    return result;
}
