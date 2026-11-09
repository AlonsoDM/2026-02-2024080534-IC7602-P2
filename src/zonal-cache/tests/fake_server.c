#include "fake_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Devuelve la linea de respuesta HTTP dependiendo del codigo q se le pase
static const char *response_line(int status) {
    switch(status) {
        case 200:
            return "HTTP/1.1 200 OK\r\n";
        case 302:
            return "HTTP/1.1 302 Found\r\nLocation: /health\r\n";
        case 404:
            return "HTTP/1.1 404 Not Found\r\n";
        case 500:
            return "HTTP/1.1 500 Internal Server Error\r\n";
        default:
            return "HTTP/1.1 500 Internal Server Error\r\n";
    }
}

// Lee la solicitud, la guarda para que la prueba pueda revisarla y luego responde
static void handle_http(fake_server *server, int client_fd) {
    char request[FAKE_SERVER_REQUEST_SIZE];
    ssize_t received = recv(client_fd, request, sizeof request - 1, 0);

    if (received > 0) {
        request[received] = '\0';

        pthread_mutex_lock(&server->lock);
        snprintf(server->last_request, sizeof server->last_request, "%s", request);
        pthread_mutex_unlock(&server->lock);
    } else {
        request[0] = '\0';
    }

    int status = server->response_status;

    if (server->require_auth && strstr(request, "Authorization: Basic ") == NULL) {
        status = 401;
    }

    const char *line = (status == 401) ? "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"zona\"\r\n"
                                       : response_line(status);

    // Sin response_body responde vacío, como en el P1
    const char *body = server->response_body ? server->response_body : "";

    char response[512];
    int length = snprintf(response, sizeof response, "%sContent-Length: %zu\r\nConnection: close\r\n\r\n", line,
                          strlen(body));

    if (length > 0) {
        send(client_fd, response, (size_t)length, MSG_NOSIGNAL);
        send(client_fd, body, strlen(body), MSG_NOSIGNAL);
    }
}

// Hilo del servidor que atiende conexiones hasta que se cierra el socket de escucha
static void *serve(void *argument) {
    fake_server *server = argument;

    for (;;) {
        int client_fd = accept(server->listen_fd, NULL, NULL);

        if (client_fd < 0) {
            break;
        }

        pthread_mutex_lock(&server->lock);
        server->connection_count++;
        pthread_mutex_unlock(&server->lock);

        if (server->speak_http) {
            handle_http(server, client_fd);
        }

        close(client_fd);
    }

    return NULL;
}

// Abre el socket de escucha 
int fake_server_start(fake_server *server) {
    server->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (server->listen_fd < 0) {
        return -1;
    }

    int enabled = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled);

    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (bind(server->listen_fd, (struct sockaddr *)&address, sizeof address) != 0 ||
        listen(server->listen_fd, 128) != 0) {
        close(server->listen_fd);
        return -1;
    }

    socklen_t length = sizeof address;
    getsockname(server->listen_fd, (struct sockaddr *)&address, &length);
    server->port = ntohs(address.sin_port);

    server->connection_count = 0;
    server->last_request[0] = '\0';
    pthread_mutex_init(&server->lock, NULL);

    if (pthread_create(&server->thread, NULL, serve, server) != 0) {
        close(server->listen_fd);
        pthread_mutex_destroy(&server->lock);
        return -1;
    }

    return 0;
}

// Cierra el socket de escucha
void fake_server_stop(fake_server *server) {
    shutdown(server->listen_fd, SHUT_RDWR); 
    close(server->listen_fd);

    pthread_join(server->thread, NULL);
    pthread_mutex_destroy(&server->lock);
}

int fake_server_connection_count(fake_server *server) {
    pthread_mutex_lock(&server->lock);
    int count = server->connection_count;
    pthread_mutex_unlock(&server->lock);

    return count;
}

void fake_server_copy_last_request(fake_server *server, char *destination, size_t size) {
    pthread_mutex_lock(&server->lock);
    snprintf(destination, size, "%s", server->last_request);
    pthread_mutex_unlock(&server->lock);
}
