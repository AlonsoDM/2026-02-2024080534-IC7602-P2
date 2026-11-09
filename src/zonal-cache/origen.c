#include "origen.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "log.h"

#define TIMEOUT_CONEXION_MS 5000L
#define TIMEOUT_ORIGEN_MS 30000L
// Lo que cabe en http_enviar_respuesta junto con la línea de estado y los headers que pone la caché
#define MAX_HEADERS_ORIGEN (HTTP_MAX_CABECERA - 256)

// Valen solo para una conexión o la caché los pone por su cuenta, así que no se reenvían en ningún sentido
static const char *const DE_CONEXION[] = {
    "Connection", "Keep-Alive", "Proxy-Connection", "Transfer-Encoding", "TE", "Trailer", "Upgrade",
    "Content-Length", "Host", "Expect", "Accept-Encoding",
};

// 1 si el header es uno de DE_CONEXION, con largo porque los de libcurl no terminan en '\0'
static int es_de_conexion(const char *nombre, size_t largo)
{
    for(size_t i = 0; i < sizeof DE_CONEXION / sizeof DE_CONEXION[0]; i++)
        if(strlen(DE_CONEXION[i]) == largo && strncasecmp(nombre, DE_CONEXION[i], largo) == 0)
            return 1;
    return 0;
}

// Guarda los headers del origen que van al cliente, con su \r\n
static size_t guardar_header(char *linea, size_t tamano, size_t cuantos, void *destino)
{
    Buffer *b = destino;
    size_t n = tamano * cuantos;
    // Una línea de estado nueva, como tras un 100 Continue, borra lo guardado
    if(n >= 5 && strncmp(linea, "HTTP/", 5) == 0) {
        b->largo = 0;
        if(b->datos)
            b->datos[0] = '\0';
        return n;
    }
    // La línea vacía del final no tiene dos puntos
    const char *dos_puntos = memchr(linea, ':', n);
    if(!dos_puntos || es_de_conexion(linea, (size_t)(dos_puntos - linea)))
        return n;
    return http_acumular(linea, tamano, cuantos, destino);
}

// curl_slist_append sin perder la lista si no hay memoria, -1 si falla
static int agregar(struct curl_slist **lista, const char *linea)
{
    struct curl_slist *mas = curl_slist_append(*lista, linea);
    if(!mas)
        return -1;
    *lista = mas;
    return 0;
}

// Le pide al origen lo mismo que pidió el cliente, -1 si no respondió y hay que dar 502
int origen_traer(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    char direccion[HTTP_MAX_CABECERA];
    char linea[HTTP_MAX_CABECERA];
    struct curl_slist *headers = NULL;
    Buffer encabezados = {.limite = MAX_HEADERS_ORIGEN};
    Buffer cuerpo = {.limite = HTTP_MAX_CUERPO};
    CURLcode resultado = CURLE_FAILED_INIT;
    // Uno por petición, no se comparten entre hilos
    CURL *curl = curl_easy_init();
    if(!curl)
        goto fin;
    // La ruta ya trae el query
    if(http_unir_url(direccion, sizeof direccion, url->origen, p->ruta) != 0) {
        resultado = CURLE_URL_MALFORMAT;
        goto fin;
    }
    // Host tampoco pasa, libcurl pone el del origen
    for(size_t i = 0; i < p->num_headers; i++) {
        if(es_de_conexion(p->headers[i].nombre, strlen(p->headers[i].nombre)))
            continue;
        snprintf(linea, sizeof linea, "%s: %s", p->headers[i].nombre, p->headers[i].valor);
        if(agregar(&headers, linea) != 0) {
            resultado = CURLE_OUT_OF_MEMORY;
            goto fin;
        }
    }
    // Un Expect vacío evita que libcurl espere un 100 Continue
    if(agregar(&headers, "Expect:") != 0) {
        resultado = CURLE_OUT_OF_MEMORY;
        goto fin;
    }

    curl_easy_setopt(curl, CURLOPT_URL, direccion);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // El origen lo escribe el usuario en la UI, así un file:///etc no lee archivos del pod
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    // Sin NOBODY libcurl se quedaría esperando el cuerpo de un HEAD
    if(strcmp(p->metodo, "HEAD") == 0)
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    else
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, p->metodo);
    if(p->largo_cuerpo > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, p->cuerpo);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)p->largo_cuerpo);
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, guardar_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &encabezados);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_acumular);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cuerpo);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, TIMEOUT_CONEXION_MS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TIMEOUT_ORIGEN_MS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // Por defecto no sigue redirects y valida el certificado
    resultado = curl_easy_perform(curl);

fin:
    if(resultado == CURLE_OK) {
        long codigo = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo);
        // Los buffers pasan a la respuesta y los libera http_liberar_respuesta
        salida->codigo = (int)codigo;
        salida->headers = encabezados.datos;
        salida->cuerpo = cuerpo.datos;
        salida->largo_cuerpo = cuerpo.largo;
    } else {
        log_error("No se pudo traer %s%s del origen, %s", url->origen, p->ruta, curl_easy_strerror(resultado));
        free(encabezados.datos);
        free(cuerpo.datos);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resultado == CURLE_OK ? 0 : -1;
}
