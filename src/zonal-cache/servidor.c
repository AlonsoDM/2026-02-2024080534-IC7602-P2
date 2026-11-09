#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "auth.h"
#include "cache.h"
#include "config.h"
#include "http.h"
#include "log.h"
#include "origen.h"
#include "servidor.h"
#include "tls.h"

// En segundos
#define TIMEOUT_CLIENTE 10

typedef struct {
    int fd;
    bool tls;
} Cliente;

// La cadena de la sección 5 del README, sin return a media cadena para que todo pase por config_soltar
static void procesar(const Conexion *cliente, const Peticion *p, Respuesta *r)
{
    if(cliente->ssl) {
        const char *sni = SSL_get_servername(cliente->ssl, TLSEXT_NAMETYPE_host_name);
        size_t largo = strcspn(p->host, ":");
        if(!sni || strlen(sni) != largo || strncasecmp(sni, p->host, largo) != 0) {
            http_respuesta_texto(r, 421, "Host no corresponde al SNI\n");
            return;
        }
    }
    const UrlConfig *url = config_buscar(p->host);
    if(!url) {
        http_respuesta_texto(r, 404, "No hay una URL configurada para este host\n");
        return;
    }
    if(cliente->ssl && !url->tls_enabled) {
        http_respuesta_texto(r, 403, "TLS no está habilitado para este host\n");
    } else if(auth_verificar(url, p, r) == 0 && cache_buscar(url, p, r) != 0) {
        if(origen_traer(url, p, r) == 0)
            cache_guardar(url, p, r);
        else
            // 502 y no 500 porque el que falló es el origen, no la caché
            http_respuesta_texto(r, 502, "El origen no respondió\n");
    }
    config_soltar(url);
}

// Lee, procesa, responde y cierra
static void *atender_cliente(void *arg)
{
    Cliente datos = *(Cliente *)arg;
    free(arg);
    Conexion cliente = {.fd = datos.fd};
    if(datos.tls && tls_aceptar(&cliente) != 0) {
        conexion_cerrar(&cliente);
        return NULL;
    }
    Peticion p;
    Respuesta r = {0};
    if(http_leer_peticion(&cliente, &p) != 0) {
        http_respuesta_texto(&r, 400, "Petición mal formada\n");
    } else {
        p.tls = cliente.ssl != NULL;
        procesar(&cliente, &p, &r);
        log_info("%s %s%s %d", p.metodo, p.host, p.ruta, r.codigo);
    }
    http_enviar_respuesta(&cliente, &r);
    http_liberar_peticion(&p);
    http_liberar_respuesta(&r);
    conexion_cerrar(&cliente);
    return NULL;
}

// Abre el socket de escucha; el llamador lo cierra
int servidor_abrir(int puerto)
{
    int servidor = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(servidor < 0) {
        log_error("No se pudo crear el socket");
        return -1;
    }

    // Reusar el puerto sin esperar el TIME_WAIT
    int si = 1;
    setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &si, sizeof si);

    struct sockaddr_in direccion = {0};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion.sin_port = htons((uint16_t)puerto);

    if(bind(servidor, (struct sockaddr *)&direccion, sizeof direccion) < 0 ||
       listen(servidor, SOMAXCONN) < 0) {
        log_error("No se pudo escuchar en el puerto %d", puerto);
        close(servidor);
        return -1;
    }
    log_info("Zonal Cache escuchando en el puerto %d", puerto);

    return servidor;
}

// Un hilo por cliente; regresa si falla el listener
int servidor_atender(int servidor, int servidor_tls)
{
    struct pollfd listeners[] = {{.fd = servidor, .events = POLLIN},
                                 {.fd = servidor_tls, .events = POLLIN}};
    for(;;) {
        if(poll(listeners, 2, -1) < 0) {
            if(errno == EINTR)
                continue;
            log_error("No se pudo esperar conexiones");
            return -1;
        }
        for(size_t i = 0; i < 2; i++) {
            if(listeners[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                log_error("Falló un listener");
                return -1;
            }
            if(!(listeners[i].revents & POLLIN))
                continue;
            int fd = accept(listeners[i].fd, NULL, NULL);
            if(fd < 0) {
                if(errno == EINTR || errno == EAGAIN || errno == ECONNABORTED)
                    continue;
                log_error("No se pudo aceptar una conexión");
                return -1;
            }
            struct timeval espera = {.tv_sec = TIMEOUT_CLIENTE};
            if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof espera) != 0 ||
               setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &espera, sizeof espera) != 0) {
                close(fd);
                continue;
            }
            Cliente *cliente = malloc(sizeof *cliente);
            if(!cliente) {
                close(fd);
                continue;
            }
            *cliente = (Cliente){.fd = fd, .tls = i == 1};
            pthread_t hilo;
            if(pthread_create(&hilo, NULL, atender_cliente, cliente) != 0) {
                free(cliente);
                close(fd);
                continue;
            }
            pthread_detach(hilo);
        }
    }
}

