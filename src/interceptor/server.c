// Recepción de server.rs del P1 con el patrón pthread_create/pthread_detach de la T2.
#include "server.h"

#include <errno.h>
#include <ldns/ldns.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "handler.h"

typedef struct {
    int socket_fd;
    struct sockaddr_in sender;
    const Config *config;
    size_t length;
    uint8_t packet[];
} Request;

static void *handle_request(void *context)
{
    Request *request = context;
    uint8_t *response = NULL;
    size_t length = 0;
    if(process_packet(request->packet, request->length, &request->sender, request->config,
                      &response, &length) != 0) {
        fprintf(stderr, "No se pudo procesar el paquete DNS\n");
    } else if(response && sendto(request->socket_fd, response, length, 0,
               (const struct sockaddr *)&request->sender, sizeof request->sender) != (ssize_t)length) {
        perror("No se pudo mandar el response DNS");
    }
    free(response);
    free(request);
    return NULL;
}

int server_run(const Config *config)
{
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in address = {.sin_family = AF_INET,
        .sin_port = htons(config->listen_port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if(bind(socket_fd, (const struct sockaddr *)&address, sizeof address) != 0) {
        perror("bind");
        close(socket_fd);
        return -1;
    }
    printf("Escuchando en UDP 0.0.0.0:%u (TTL interno: %u segundos)\n",
           (unsigned)config->listen_port, (unsigned)config->dns_ttl_seconds);
    fflush(stdout);

    uint8_t buffer[LDNS_MAX_PACKETLEN];
    for(;;) {
        struct sockaddr_in sender;
        socklen_t sender_length = sizeof sender;
        ssize_t received = recvfrom(socket_fd, buffer, sizeof buffer, 0,
                                    (struct sockaddr *)&sender, &sender_length);
        if(received < 0) {
            if(errno != EINTR)
                perror("recvfrom");
            continue;
        }
        Request *request = malloc(sizeof *request + (size_t)received);
        if(!request) {
            fprintf(stderr, "No hay memoria para atender el paquete DNS\n");
            continue;
        }
        request->socket_fd = socket_fd;
        request->sender = sender;
        request->config = config;
        request->length = (size_t)received;
        memcpy(request->packet, buffer, request->length);
        pthread_t thread;
        int error = pthread_create(&thread, NULL, handle_request, request);
        if(error) {
            fprintf(stderr, "No se pudo crear el hilo: %s\n", strerror(error));
            free(request);
            continue;
        }
        pthread_detach(thread);
    }
}
