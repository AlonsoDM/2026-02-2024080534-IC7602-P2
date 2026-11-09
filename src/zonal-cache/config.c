#include "config.h"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "http.h"
#include "log.h"

#define RUTA_CONFIG "/api/cache/config"
#define MAX_RESPUESTA (1024 * 1024)
#define TIMEOUT_CONEXION_MS 3000L
#define TIMEOUT_PETICION_MS 5000L
// El entero más grande que JavaScript representa exacto
#define MAX_ENTERO_JSON 9007199254740991.0

// usos cuenta la referencia global más cada petición que la tiene en la mano
struct Configuracion {
    int usos;
    size_t num_urls;
    UrlConfig urls[];
};

static pthread_mutex_t candado = PTHREAD_MUTEX_INITIALIZER;
static Configuracion *actual;
static ConfigCache ajustes;

static const char *read_required(const char *name, int *errors) {
    const char *value = getenv(name);
    if(value == NULL || value[0] == '\0') {
        log_error("Falta la variable de entorno %s", name);
        (*errors)++;
        return NULL;
    }

    return value;
}

static int read_int(const char *name, int *value, int *errors) {
    const char *raw = read_required(name, errors);
    if(raw == NULL) {
        return -1;
    }

    char *end = NULL;
    errno = 0;
    long parsed = strtol(raw, &end, 10);

    if(errno != 0 || end == raw || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        log_error("La variable %s debe ser un entero, se recibió \"%s\"", name, raw);
        (*errors)++;
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static void validate_positive(const char *name, int value, int *errors) {
    if(value <= 0) {
        log_error("La variable %s debe ser mayor que cero, se recibió %d", name, value);
        (*errors)++;
    }
}

// Lee y valida las variables de entorno, -1 si falta alguna o no sirve
int config_cargar(ConfigCache *config)
{
    int errors = 0;
    int puerto = 0;
    if(read_int("CACHE_PORT", &puerto, &errors) == 0 && (puerto < 1 || puerto > 65535)) {
        log_error("CACHE_PORT debe estar entre 1 y 65535, se recibió %d", puerto);
        errors++;
    }
    int puerto_tls = 0;
    if(read_int("CACHE_TLS_PORT", &puerto_tls, &errors) == 0 && (puerto_tls < 1 || puerto_tls > 65535)) {
        log_error("CACHE_TLS_PORT debe estar entre 1 y 65535, se recibió %d", puerto_tls);
        errors++;
    }
    if(puerto > 0 && puerto == puerto_tls) {
        log_error("CACHE_PORT y CACHE_TLS_PORT deben ser distintos");
        errors++;
    }
    const char *ca_cert_file = read_required("TLS_CA_CERT_FILE", &errors);
    const char *ca_key_file = read_required("TLS_CA_KEY_FILE", &errors);
    const char *api_url = read_required("NODE_API_URL", &errors);
    // libcurl también abriría ftp o file
    if(api_url && strncmp(api_url, "https://", 8) != 0 && strncmp(api_url, "http://", 7) != 0) {
        log_error("NODE_API_URL debe iniciar con http:// o https://, se recibió \"%s\"", api_url);
        errors++;
    }
    const char *api_key = read_required("NODE_API_KEY", &errors);
    // Formulario de la UI al que se manda una URL con user_password sin sesión
    const char *ui_login_url = read_required("UI_LOGIN_URL", &errors);
    if(ui_login_url && strncmp(ui_login_url, "https://", 8) != 0 && strncmp(ui_login_url, "http://", 7) != 0) {
        log_error("UI_LOGIN_URL debe iniciar con http:// o https://, se recibió \"%s\"", ui_login_url);
        errors++;
    }
    int recarga = 0;
    if(read_int("CONFIG_REFRESH_SECONDS", &recarga, &errors) == 0)
        validate_positive("CONFIG_REFRESH_SECONDS", recarga, &errors);
    int purga = 0;
    if(read_int("PURGE_INTERVAL_SECONDS", &purga, &errors) == 0)
        validate_positive("PURGE_INTERVAL_SECONDS", purga, &errors);
    const char *directorio = read_required("CACHE_DIR", &errors);
    if(errors > 0)
        return -1;
    config->puerto = puerto;
    config->puerto_tls = puerto_tls;
    config->ca_cert_file = ca_cert_file;
    config->ca_key_file = ca_key_file;
    config->api_url = api_url;
    config->api_key = api_key;
    config->ui_login_url = ui_login_url;
    config->segundos_recarga = recarga;
    config->segundos_purga = purga;
    config->directorio = directorio;
    return 0;
}

// Puntaje del calce, 0 si no calza, la exacta gana y entre wildcards la más larga
static size_t calce(const char *patron, const char *host, size_t largo_host)
{
    if(patron[0] == '*' && patron[1] == '.') {
        // El patrón de URL cubre el dominio raíz y sus subdominios.
        const char *sufijo = patron + 1;
        size_t largo_sufijo = strlen(sufijo);
        if(largo_host == largo_sufijo - 1 &&
           strncasecmp(host, sufijo + 1, largo_host) == 0)
            return largo_sufijo;
        if(largo_host > largo_sufijo &&
           strncasecmp(host + largo_host - largo_sufijo, sufijo, largo_sufijo) == 0)
            return largo_sufijo;
        return 0;
    }
    if(strlen(patron) == largo_host && strncasecmp(patron, host, largo_host) == 0)
        return SIZE_MAX;
    return 0;
}

// La URL que le toca al Host, NULL si no hay, y la que devuelve se suelta con config_soltar
const UrlConfig *config_buscar(const char *host)
{
    // El puerto del Host no cuenta
    size_t largo_host = strcspn(host, ":");
    const UrlConfig *mejor = NULL;
    size_t mejor_calce = 0;
    pthread_mutex_lock(&candado);
    // actual es NULL si nunca se pudo cargar
    for(size_t i = 0; actual && i < actual->num_urls; i++) {
        size_t c = calce(actual->urls[i].patron, host, largo_host);
        if(c > mejor_calce) {
            mejor_calce = c;
            mejor = &actual->urls[i];
        }
    }
    // Así nadie la libera mientras la petición la usa
    if(mejor)
        actual->usos++;
    pthread_mutex_unlock(&candado);
    return mejor;
}

bool config_patron_tls_habilitado(const char *patron)
{
    bool habilitado = false;
    pthread_mutex_lock(&candado);
    for(size_t i = 0; actual && i < actual->num_urls; i++) {
        if(actual->urls[i].tls_enabled && strcasecmp(actual->urls[i].patron, patron) == 0) {
            habilitado = true;
            break;
        }
    }
    pthread_mutex_unlock(&candado);
    return habilitado;
}

// Libera los textos y los tipos de archivo de una URL
static void liberar_url(UrlConfig *u)
{
    free(u->patron);
    free(u->origen);
    free(u->politica);
    free(u->auth);
    for(size_t i = 0; i < u->num_tipos; i++)
        free(u->tipos[i].extension);
    free(u->tipos);
}

// Libera una configuración entera
static void liberar(Configuracion *c)
{
    for(size_t i = 0; i < c->num_urls; i++)
        liberar_url(&c->urls[i]);
    free(c);
}

// Resta un uso y libera la configuración si ya la reemplazaron y nadie más la tiene
void config_soltar(const UrlConfig *url)
{
    Configuracion *c = url->dueno;
    pthread_mutex_lock(&candado);
    int quedan = --c->usos;
    pthread_mutex_unlock(&candado);
    if(quedan == 0)
        liberar(c);
}

// Cambia la configuración vigente sin cortar a las peticiones que usan la vieja
void config_instalar(Configuracion *nueva)
{
    // Ser la vigente cuenta como un uso
    nueva->usos = 1;
    pthread_mutex_lock(&candado);
    Configuracion *vieja = actual;
    actual = nueva;
    int quedan = vieja ? --vieja->usos : 1;
    pthread_mutex_unlock(&candado);
    if(quedan == 0)
        liberar(vieja);
}

// Copia un texto no vacío del JSON, -1 si no viene o no es texto
static int leer_texto(const cJSON *objeto, const char *nombre, char **destino)
{
    const cJSON *valor = cJSON_GetObjectItemCaseSensitive(objeto, nombre);
    if(!cJSON_IsString(valor) || !valor->valuestring[0])
        return -1;
    *destino = strdup(valor->valuestring);
    return *destino ? 0 : -1;
}

// Un número del JSON entre minimo y maximo, -1 si no viene o se sale
static int leer_numero(const cJSON *objeto, const char *nombre, double minimo, double maximo, double *destino)
{
    const cJSON *valor = cJSON_GetObjectItemCaseSensitive(objeto, nombre);
    if(!cJSON_IsNumber(valor) || valor->valuedouble < minimo || valor->valuedouble > maximo)
        return -1;
    *destino = valor->valuedouble;
    return 0;
}

// Llena una UrlConfig con un elemento de "urls", -1 si le falta algo o algo no sirve
static int parsear_url(const cJSON *json, UrlConfig *u)
{
    double id, tamano;
    const cJSON *tls = cJSON_GetObjectItemCaseSensitive(json, "tls_enabled");
    if(!cJSON_IsBool(tls))
        return -1;
    u->tls_enabled = cJSON_IsTrue(tls);
    // Si falla a medias, lo que ya se copió lo libera liberar_url
    if(leer_numero(json, "id", 0, INT_MAX, &id) != 0 ||
       leer_texto(json, "pattern", &u->patron) != 0 ||
       leer_texto(json, "origin", &u->origen) != 0 ||
       leer_numero(json, "cache_size_bytes", 1, MAX_ENTERO_JSON, &tamano) != 0 ||
       leer_texto(json, "replacement_policy", &u->politica) != 0 ||
       leer_texto(json, "auth", &u->auth) != 0)
        return -1;
    u->id = (int)id;
    u->tamano_maximo = (size_t)tamano;

    const cJSON *tipos = cJSON_GetObjectItemCaseSensitive(json, "file_types");
    if(!cJSON_IsArray(tipos))
        return -1;
    // Sin tipos es válida, solo que nunca guarda nada en disco
    size_t total = (size_t)cJSON_GetArraySize(tipos);
    if(total == 0)
        return 0;
    u->tipos = calloc(total, sizeof *u->tipos);
    if(!u->tipos)
        return -1;
    // Después del calloc para que liberar_url no recorra un arreglo que no existe
    u->num_tipos = total;
    size_t i = 0;
    const cJSON *tipo;
    cJSON_ArrayForEach(tipo, tipos) {
        double ttl;
        if(leer_texto(tipo, "extension", &u->tipos[i].extension) != 0 ||
           leer_numero(tipo, "ttl_seconds", 1, INT_MAX, &ttl) != 0)
            return -1;
        u->tipos[i++].ttl_segundos = (int)ttl;
    }
    return 0;
}

// Arma una configuración con el JSON del REST API, NULL si el JSON no sirve
Configuracion *config_parsear(const char *texto)
{
    cJSON *json = cJSON_Parse(texto);
    const cJSON *urls = cJSON_GetObjectItemCaseSensitive(json, "urls");
    Configuracion *c = NULL;
    // Una sola reserva, las URLs van pegadas al final del struct
    if(cJSON_IsArray(urls))
        c = calloc(1, sizeof *c + (size_t)cJSON_GetArraySize(urls) * sizeof(UrlConfig));
    if(!c) {
        log_error("La configuración del REST API no es un JSON con la lista \"urls\"");
        cJSON_Delete(json);
        return NULL;
    }
    size_t posicion = 0;
    const cJSON *elemento;
    cJSON_ArrayForEach(elemento, urls) {
        posicion++;
        UrlConfig *u = &c->urls[c->num_urls];
        // Una URL mala se salta sin tumbar las demás
        if(parsear_url(elemento, u) != 0) {
            log_error("Se ignora la URL %zu de la configuración porque le falta un campo o no es válido", posicion);
            liberar_url(u);
            memset(u, 0, sizeof *u);
            continue;
        }
        u->dueno = c;
        c->num_urls++;
    }
    cJSON_Delete(json);
    return c;
}

// Pide GET /api/cache/config con la llave de componente, NULL si no se pudo
static char *pedir_config(void)
{
    char url[512], header[512];
    int n_header = snprintf(header, sizeof header, "x-api-key: %s", ajustes.api_key);
    if(http_unir_url(url, sizeof url, ajustes.api_url, RUTA_CONFIG) != 0 ||
       n_header < 0 || (size_t)n_header >= sizeof header) {
        log_error("NODE_API_URL o NODE_API_KEY son demasiado largas");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    struct curl_slist *headers = curl_slist_append(NULL, header);
    Buffer respuesta = {.limite = MAX_RESPUESTA};
    long codigo = 0;
    CURLcode resultado = CURLE_FAILED_INIT;
    if(curl && headers) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_acumular);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, TIMEOUT_CONEXION_MS);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TIMEOUT_PETICION_MS);
        // Con hilos, una señal de libcurl le puede caer a cualquiera
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        resultado = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(resultado != CURLE_OK)
        log_error("No se pudo pedir la configuración al REST API, %s", curl_easy_strerror(resultado));
    else if(codigo != 200)
        log_error("El REST API respondió %ld al pedir la configuración", codigo);
    if(resultado != CURLE_OK || codigo != 200) {
        free(respuesta.datos);
        return NULL;
    }
    return respuesta.datos;
}

// Pide la configuración y la instala, si algo falla se queda la que había
static void recargar(void)
{
    char *texto = pedir_config();
    Configuracion *nueva = texto ? config_parsear(texto) : NULL;
    free(texto);
    if(!nueva) {
        log_error("Se mantiene la configuración que había");
        return;
    }
    // Antes de instalar, después otro hilo ya la puede estar usando
    log_info("Configuración cargada con %zu URL", nueva->num_urls);
    config_instalar(nueva);
}

// Vuelve a pedir la configuración cada CONFIG_REFRESH_SECONDS
static void *recargar_siempre(void *arg)
{
    (void)arg;
    for(;;) {
        sleep((unsigned)ajustes.segundos_recarga);
        recargar();
    }
    return NULL;
}

// Primera carga y el hilo que recarga, si el API no responde se arranca igual dando 404
int config_iniciar(const ConfigCache *config)
{
    ajustes = *config;
    recargar();
    pthread_t hilo;
    if(pthread_create(&hilo, NULL, recargar_siempre, NULL) != 0) {
        log_error("No se pudo crear el hilo que recarga la configuración");
        return -1;
    }
    pthread_detach(hilo);
    return 0;
}
