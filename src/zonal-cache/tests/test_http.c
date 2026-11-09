#include <criterion/criterion.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"

// Le pasa el texto a http_leer_peticion por un par de sockets conectados
static int leer(const char *texto, size_t largo, Peticion *p)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    cr_assert_eq(write(par[1], texto, largo), (ssize_t)largo);
    // Cerrar el otro extremo hace que una petición incompleta termine en vez de esperar
    close(par[1]);
    Conexion conexion = {.fd = par[0]};
    int resultado = http_leer_peticion(&conexion, p);
    conexion_cerrar(&conexion);
    return resultado;
}

Test(http, get_valida)
{
    const char *texto = "GET /img/logo.png?v=2 HTTP/1.1\r\nHost: www.marca.com\r\nAccept:  */* \r\n\r\n";
    Peticion p;
    cr_assert_eq(leer(texto, strlen(texto), &p), 0);
    cr_assert_str_eq(p.metodo, "GET");
    cr_assert_str_eq(p.ruta, "/img/logo.png?v=2");
    cr_assert_str_eq(p.host, "www.marca.com");
    cr_assert_str_eq(http_header(&p, "accept"), "*/*");
    cr_assert_null(http_header(&p, "Cookie"));
    cr_assert_eq(p.largo_cuerpo, 0);
    http_liberar_peticion(&p);
}

Test(http, post_con_cuerpo)
{
    const char *texto = "POST /api HTTP/1.1\r\nHost: api.marca.com\r\nContent-Length: 11\r\n\r\n{\"a\":\"b c\"}";
    Peticion p;
    cr_assert_eq(leer(texto, strlen(texto), &p), 0);
    cr_assert_eq(p.largo_cuerpo, 11);
    cr_assert_eq(memcmp(p.cuerpo, "{\"a\":\"b c\"}", 11), 0);
    http_liberar_peticion(&p);
}

Test(http, mal_formadas)
{
    const char *malas[] = {
        "GET / HTTP/1.1\r\n\r\n",
        "GET /\r\nHost: a\r\n\r\n",
        "GET / HTTP/1.1 extra\r\nHost: a\r\n\r\n",
        "GET / HTTP/2.0\r\nHost: a\r\n\r\n",
        "GET http://a/ HTTP/1.1\r\nHost: a\r\n\r\n",
        "GET / HTTP/1.1\r\nHost a\r\n\r\n",
        "GET / HTTP/1.1\r\nHost:\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -3\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nabc",
        "GET / HTTP/1.1\r\nHost: a\r\n",
        "",
    };
    for(size_t i = 0; i < sizeof malas / sizeof malas[0]; i++) {
        Peticion p;
        cr_expect_eq(leer(malas[i], strlen(malas[i]), &p), -1, "debió rechazar la número %zu", i);
        http_liberar_peticion(&p);
    }
}

Test(http, cabecera_demasiado_grande)
{
    static char texto[HTTP_MAX_CABECERA + 100];
    memset(texto, 'a', sizeof texto);
    Peticion p;
    cr_assert_eq(leer(texto, sizeof texto, &p), -1);
    http_liberar_peticion(&p);
}

Test(http, respuesta_con_largo)
{
    int par[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, par), 0);
    Respuesta r = {0};
    http_respuesta_texto(&r, 404, "nada\n");
    Conexion conexion = {.fd = par[0]};
    cr_assert_eq(http_enviar_respuesta(&conexion, &r), 0);
    http_liberar_respuesta(&r);
    conexion_cerrar(&conexion);

    char recibido[512] = "";
    cr_assert_gt(read(par[1], recibido, sizeof recibido - 1), 0);
    close(par[1]);
    cr_assert_str_eq(recibido, "HTTP/1.1 404 Not Found\r\n"
                               "Content-Type: text/plain; charset=utf-8\r\n"
                               "Content-Length: 5\r\n"
                               "Connection: close\r\n\r\n"
                               "nada\n");
}
