#ifndef FAKE_SERVER_H
#define FAKE_SERVER_H

#include <pthread.h>

#define FAKE_SERVER_REQUEST_SIZE 2048

typedef struct {
    int listen_fd;
    int port;
    pthread_t thread;

    int speak_http;
    int response_status;
    int require_auth;
    const char *response_body;

    int connection_count;
    char last_request[FAKE_SERVER_REQUEST_SIZE];
    pthread_mutex_t lock;
} fake_server;

int fake_server_start(fake_server *server);

void fake_server_stop(fake_server *server);

int fake_server_connection_count(fake_server *server);

void fake_server_copy_last_request(fake_server *server, char *destination, size_t size);

#endif
