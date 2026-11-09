#include <criterion/criterion.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "fake_server.h"
#include "http.h"
#include "origen.h"

// Arma una Peticion con el texto que mandaría un cliente
static void leer(const char *texto, Peticion *p)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    cr_assert_eq(write(par[1], texto, strlen(texto)), (ssize_t)strlen(texto));
    close(par[1]);
    Conexion conexion = {.fd = par[0]};
    cr_assert_eq(http_leer_peticion(&conexion, p), 0);
    conexion_cerrar(&conexion);
}

// Pasa la petición por origen_traer contra un origen falso y copia lo que le llegó al origen
static int traer(fake_server *origen, const char *texto, Respuesta *r, char *recibida, size_t tam)
{
    cr_assert_eq(fake_server_start(origen), 0);
    char base[64];
    snprintf(base, sizeof base, "http://127.0.0.1:%d/", origen->port);
    UrlConfig url = {.origen = base};
    Peticion p;
    leer(texto, &p);
    int resultado = origen_traer(&url, &p, r);
    http_liberar_peticion(&p);
    fake_server_copy_last_request(origen, recibida, tam);
    fake_server_stop(origen);
    return resultado;
}

Test(origen, reenvia_ruta_y_headers)
{
    fake_server origen = {.speak_http = 1, .response_status = 200, .response_body = "hola"};
    Respuesta r = {0};
    char recibida[FAKE_SERVER_REQUEST_SIZE];
    cr_assert_eq(traer(&origen,
                       "GET /img/logo.png?v=2 HTTP/1.1\r\nHost: www.marca.com\r\nX-Prueba: si\r\n"
                       "Connection: keep-alive\r\nAccept-Encoding: gzip\r\n\r\n",
                       &r, recibida, sizeof recibida), 0);
    cr_expect_eq(r.codigo, 200);
    cr_assert_eq(r.largo_cuerpo, 4);
    cr_expect_eq(memcmp(r.cuerpo, "hola", 4), 0);
    // El origen falso manda Content-Length y Connection, y esos los pone la caché
    cr_expect(!r.headers || (!strstr(r.headers, "Content-Length") && !strstr(r.headers, "Connection")));
    cr_expect_not_null(strstr(recibida, "GET /img/logo.png?v=2 HTTP/1.1\r\n"));
    cr_expect_not_null(strstr(recibida, "X-Prueba: si\r\n"));
    cr_expect_not_null(strstr(recibida, "Host: 127.0.0.1:"));
    cr_expect_null(strstr(recibida, "www.marca.com"));
    cr_expect_null(strstr(recibida, "keep-alive"));
    cr_expect_null(strstr(recibida, "gzip"));
    http_liberar_respuesta(&r);
}

Test(origen, reenvia_metodo_y_cuerpo)
{
    fake_server origen = {.speak_http = 1, .response_status = 200};
    Respuesta r = {0};
    char recibida[FAKE_SERVER_REQUEST_SIZE];
    cr_assert_eq(traer(&origen,
                       "PUT /api/items/3 HTTP/1.1\r\nHost: api.marca.com\r\nContent-Type: application/json\r\n"
                       "Content-Length: 11\r\n\r\n{\"a\":\"b c\"}",
                       &r, recibida, sizeof recibida), 0);
    cr_expect_not_null(strstr(recibida, "PUT /api/items/3 HTTP/1.1\r\n"));
    cr_expect_not_null(strstr(recibida, "Content-Type: application/json\r\n"));
    cr_expect_not_null(strstr(recibida, "Content-Length: 11\r\n"));
    cr_expect_null(strstr(recibida, "Expect"));
    http_liberar_respuesta(&r);
}

Test(origen, no_sigue_redirects)
{
    fake_server origen = {.speak_http = 1, .response_status = 302};
    Respuesta r = {0};
    char recibida[FAKE_SERVER_REQUEST_SIZE];
    cr_assert_eq(traer(&origen, "GET /viejo HTTP/1.1\r\nHost: a\r\n\r\n", &r, recibida, sizeof recibida), 0);
    cr_expect_eq(r.codigo, 302);
    cr_assert_not_null(r.headers);
    cr_expect_not_null(strstr(r.headers, "Location: /health\r\n"));
    http_liberar_respuesta(&r);
}

Test(origen, origen_caido_o_protocolo_prohibido)
{
    // Se abre y se cierra un servidor para tener un puerto donde nadie escucha
    fake_server cerrado = {0};
    cr_assert_eq(fake_server_start(&cerrado), 0);
    char base[64];
    snprintf(base, sizeof base, "http://127.0.0.1:%d", cerrado.port);
    fake_server_stop(&cerrado);

    UrlConfig url = {.origen = base};
    Peticion p;
    Respuesta r = {0};
    leer("GET /passwd HTTP/1.1\r\nHost: a\r\n\r\n", &p);
    cr_expect_eq(origen_traer(&url, &p, &r), -1);

    char archivo[] = "file:///etc";
    url.origen = archivo;
    cr_expect_eq(origen_traer(&url, &p, &r), -1);
    cr_expect_null(r.cuerpo);
    http_liberar_peticion(&p);
}
