#include <criterion/criterion.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "servidor.h"

// Un puerto por prueba porque Criterion puede correrlas en paralelo
#define PUERTO_ERRORES  9871
#define PUERTO_PARALELO 9872
#define PUERTO_LISTENER_A 9873
#define PUERTO_LISTENER_B 9874

static void *correr_servidor(void *arg)
{
    int servidor = (int)(intptr_t)arg;
    servidor_atender(servidor, -1);
    close(servidor);
    return NULL;
}

static void arrancar(int puerto)
{
    int servidor = servidor_abrir(puerto);
    cr_assert_geq(servidor, 0);
    pthread_t hilo;
    cr_assert_eq(pthread_create(&hilo, NULL, correr_servidor, (void *)(intptr_t)servidor), 0);
    pthread_detach(hilo);
}

// Se conecta reintentando mientras el servidor arranca
static int conectar(int puerto)
{
    struct sockaddr_in direccion = {0};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    direccion.sin_port = htons((uint16_t)puerto);
    for(int intento = 0; intento < 50; intento++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(connect(fd, (struct sockaddr *)&direccion, sizeof direccion) == 0) {
            // Si el servidor no contesta la prueba falla en vez de quedarse colgada
            struct timeval espera = {.tv_sec = 3};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera);
            return fd;
        }
        close(fd);
        usleep(100000);
    }
    cr_assert_fail("el servidor no arrancó en el puerto %d", puerto);
    return -1;
}

// Manda la petición por una conexión nueva y devuelve la línea de estado
static void pedir(int puerto, const char *texto, char *linea, size_t tam)
{
    int fd = conectar(puerto);
    cr_assert_eq(send(fd, texto, strlen(texto), 0), (ssize_t)strlen(texto));
    ssize_t n = recv(fd, linea, tam - 1, 0);
    close(fd);
    linea[n > 0 ? n : 0] = '\0';
    linea[strcspn(linea, "\r")] = '\0';
}

Test(servidor, errores_sin_caerse)
{
    arrancar(PUERTO_ERRORES);
    char linea[256];

    pedir(PUERTO_ERRORES, "GET / HTTP/1.1\r\nHost: www.marca.com\r\n\r\n", linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 404 Not Found");

    pedir(PUERTO_ERRORES, "basura\r\n\r\n", linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 400 Bad Request");

    pedir(PUERTO_ERRORES, "GET / HTTP/1.1\r\nHost: www.marca.com\r\n\r\n", linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 404 Not Found");
}

// Con un solo hilo, el cliente callado bloquearía al segundo
Test(servidor, atiende_en_paralelo)
{
    arrancar(PUERTO_PARALELO);
    int callado = conectar(PUERTO_PARALELO);

    char linea[256];
    pedir(PUERTO_PARALELO, "GET / HTTP/1.1\r\nHost: www.marca.com\r\n\r\n", linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 404 Not Found");
    close(callado);
}

// Los listeners comparten la atención HTTP sin bloquearse entre sí
Test(servidor, dos_listeners)
{
    arrancar(PUERTO_LISTENER_A);
    arrancar(PUERTO_LISTENER_B);
    int callado = conectar(PUERTO_LISTENER_A);
    char linea[256];
    const char *peticion = "GET / HTTP/1.1\r\nHost: www.marca.com\r\n\r\n";
    pedir(PUERTO_LISTENER_B, peticion, linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 404 Not Found");
    pedir(PUERTO_LISTENER_A, peticion, linea, sizeof linea);
    cr_assert_str_eq(linea, "HTTP/1.1 404 Not Found");
    close(callado);
}

Test(servidor, falla_si_el_puerto_esta_ocupado)
{
    int servidor = servidor_abrir(0);
    cr_assert_geq(servidor, 0);
    struct sockaddr_in direccion;
    socklen_t largo = sizeof direccion;
    cr_assert_eq(getsockname(servidor, (struct sockaddr *)&direccion, &largo), 0);
    int segundo = servidor_abrir(ntohs(direccion.sin_port));
    if(segundo >= 0)
        close(segundo);
    close(servidor);
    cr_assert_eq(segundo, -1);
}
