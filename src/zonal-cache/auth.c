#include "auth.h"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "log.h"

// Guarda la dirección del REST API, la llave de componente y el formulario de la UI, antes de crear los threads de la cache

#define RUTA_VERIFICAR_LLAVE "/api/cache/api-keys/verify"
#define RUTA_VERIFICAR_SESION "/api/cache/sessions/verify"
#define RUTA_CALLBACK "/__auth/callback"
#define COOKIE_SESION "zc_session"
// El Worker da 64 caracteres hex; con margen pero sin dejar pasar cualquier cosa a Set-Cookie
#define MAX_SESION 128
#define MAX_RESPUESTA 4096
#define TIMEOUT_CONEXION_MS 3000L
#define TIMEOUT_PETICION_MS 5000L

static const char *api_url;
static const char *api_key;
static const char *ui_login_url;

void auth_iniciar(const ConfigCache *config)
{
    api_url = config->api_url;
    api_key = config->api_key;
    ui_login_url = config->ui_login_url;
}

// POST de un JSON a una ruta del API deja en *salida la respuesta si fue 200 (0) o -1 si no se pudo
static int preguntar_api(const char *ruta, cJSON *json, cJSON **salida)
{
    *salida = NULL;
    char url[512], header[512];
    int n_header = snprintf(header, sizeof header, "x-api-key: %s", api_key ? api_key : "");
    if(!api_url || http_unir_url(url, sizeof url, api_url, ruta) != 0 ||
       n_header < 0 || (size_t)n_header >= sizeof header) {
        log_error("NODE_API_URL o NODE_API_KEY no sirven para llamar %s", ruta);
        return -1;
    }

    // Con cJSON para que una llave con comillas no rompa el JSON
    char *cuerpo = json ? cJSON_PrintUnformatted(json) : NULL;
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = curl_slist_append(NULL, header);
    if(headers)
        headers = curl_slist_append(headers, "Content-Type: application/json");
    Buffer respuesta = {.limite = MAX_RESPUESTA};
    long codigo = 0;
    CURLcode resultado = CURLE_FAILED_INIT;
    if(curl && headers && cuerpo) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cuerpo);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_acumular);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, TIMEOUT_CONEXION_MS);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TIMEOUT_PETICION_MS);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        resultado = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(cuerpo);

    if(resultado != CURLE_OK)
        log_error("No se pudo llamar %s, %s", ruta, curl_easy_strerror(resultado));
    else if(codigo != 200)
        log_error("El REST API respondio %ld en %s", codigo, ruta);
    else
        *salida = cJSON_Parse(respuesta.datos);
    free(respuesta.datos);
    return *salida ? 0 : -1;
}

// Lee "valid" de la respuesta del API (1 si, 0 no, -1 no se sabe)
static int leer_valid(const cJSON *r, const char *ruta)
{
    const cJSON *campo = cJSON_GetObjectItemCaseSensitive(r, "valid");
    if(cJSON_IsBool(campo))
        return cJSON_IsTrue(campo) ? 1 : 0;
    log_error("El REST API no devolvio \"valid\" en %s", ruta);
    return -1;
}

// Pregunta al API si la llave es de esta URL (1 si, 0 no, -1 no se sabe)
static int llave_valida(int url_id, const char *llave)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *r = NULL;
    int valida = -1;
    if(json && cJSON_AddNumberToObject(json, "url_id", url_id) && cJSON_AddStringToObject(json, "key", llave) &&
       preguntar_api(RUTA_VERIFICAR_LLAVE, json, &r) == 0)
        valida = leer_valid(r, RUTA_VERIFICAR_LLAVE);
    cJSON_Delete(json);
    cJSON_Delete(r);
    return valida;
}

// Pregunta al API si la sesión es de esta URL y sigue vigente (1 si, 0 no, -1 no se pudo saber)
static int sesion_valida(int url_id, const char *sesion)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *r = NULL;
    int valida = -1;
    if(json && cJSON_AddNumberToObject(json, "url_id", url_id) &&
       cJSON_AddStringToObject(json, "session", sesion) && preguntar_api(RUTA_VERIFICAR_SESION, json, &r) == 0)
        valida = leer_valid(r, RUTA_VERIFICAR_SESION);
    cJSON_Delete(json);
    cJSON_Delete(r);
    return valida;
}

static int x_api_key(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    const char *llave = http_header(p, "x-api-key");
    if(!llave || !llave[0]) {
        http_respuesta_texto(salida, 401, "Falta el header x-api-key\n");
        return -1;
    }
    int valida = llave_valida(url->id, llave);
    if(valida == 1)
        return 0;
    // Si el API no contesta no se deja pasar, pero tampoco es culpa de la llave
    if(valida == 0)
        http_respuesta_texto(salida, 401, "Llave no valida\n");
    else
        http_respuesta_texto(salida, 503, "No se pudo verificar la llave\n");
    return -1;
}

// Usuario y contraseña

// Escribe texto con %XX en todo lo que no sea letra, dígito o -._~; -1 si no cabe
static int codificar(char *destino, size_t tam, const char *texto)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for(const unsigned char *c = (const unsigned char *)texto; *c; c++) {
        int simple = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                     *c == '-' || *c == '.' || *c == '_' || *c == '~';
        if(n + (simple ? 1 : 3) >= tam)
            return -1;
        if(simple) {
            destino[n++] = (char)*c;
        } else {
            destino[n++] = '%';
            destino[n++] = hex[*c >> 4];
            destino[n++] = hex[*c & 15];
        }
    }
    destino[n] = '\0';
    return 0;
}

static int valor_hex(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Busca un parámetro del query y lo deja decodificado
// -1 si no viene, no cabe o trae un %XX roto
static int parametro(const char *query, const char *nombre, char *destino, size_t tam)
{
    size_t largo_nombre = strlen(nombre);
    for(const char *par = query; par && *par; par = strchr(par, '&') ? strchr(par, '&') + 1 : NULL) {
        if(strncmp(par, nombre, largo_nombre) != 0 || par[largo_nombre] != '=')
            continue;
        size_t n = 0;
        for(const char *c = par + largo_nombre + 1; *c && *c != '&'; c++) {
            if(n + 1 >= tam)
                return -1;
            if(*c == '%') {
                int alto = valor_hex(c[1]), bajo = alto < 0 ? -1 : valor_hex(c[2]);
                if(bajo < 0)
                    return -1;
                destino[n++] = (char)(alto * 16 + bajo);
                c += 2;
            } else {
                destino[n++] = *c;
            }
        }
        destino[n] = '\0';
        return 0;
    }
    return -1;
}

// Copia el valor de la cookie de la sesion; -1 si no viene
// Puede haber varios headers Cookie
static int cookie_sesion(const Peticion *p, char *destino, size_t tam)
{
    size_t largo_nombre = strlen(COOKIE_SESION);
    for(size_t i = 0; i < p->num_headers; i++) {
        if(strcasecmp(p->headers[i].nombre, "Cookie") != 0)
            continue;
        // "a=1; zc_session=abc; b=2"
        for(const char *c = p->headers[i].valor; c; c = strchr(c, ';') ? strchr(c, ';') + 1 : NULL) {
            while(*c == ' ')
                c++;
            if(strncmp(c, COOKIE_SESION, largo_nombre) != 0 || c[largo_nombre] != '=')
                continue;
            const char *valor = c + largo_nombre + 1;
            size_t largo = strcspn(valor, "; ");
            if(largo == 0 || largo >= tam)
                return -1;
            memcpy(destino, valor, largo);
            destino[largo] = '\0';
            return 0;
        }
    }
    return -1;
}

// Una sesion solo puede traer letras y digitos
static int sesion_bien_formada(const char *s)
{
    if(!s[0])
        return 0;
    for(; *s; s++)
        if(!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')))
            return 0;
    return 1;
}

// Solo rutas de este mismo host: "/algo", nunca "//otro.com" ni "/\otro.com" ni con caracteres de control
static int ruta_local(const char *ruta)
{
    if(ruta[0] != '/' || ruta[1] == '/' || ruta[1] == '\\')
        return 0;
    for(const unsigned char *c = (const unsigned char *)ruta; *c; c++)
        if(*c < 0x20 || *c == 0x7f)
            return 0;
    return 1;
}

// Deja un 302 con Location y, si se da: Set-Cookie
static void redirigir(Respuesta *salida, const char *destino, const char *cookie)
{
    http_respuesta_texto(salida, 302, "Redirigiendo\n");
    const char *tipo = salida->headers ? salida->headers : "";
    size_t tam = strlen(tipo) + strlen(destino) + (cookie ? strlen(cookie) : 0) + 64;
    char *headers = malloc(tam);
    if(!headers) {
        http_respuesta_texto(salida, 503, "Sin memoria\n");
        return;
    }
    snprintf(headers, tam, "%sLocation: %s\r\n%s%s%s", tipo, destino,
             cookie ? "Set-Cookie: " : "", cookie ? cookie : "", cookie ? "\r\n" : "");
    free(salida->headers);
    salida->headers = headers;
}

// Redirige al login de la UI e incluye la URL de retorno para continuar despues de autenticarse
static void mandar_al_login(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    char ruta[HTTP_MAX_CABECERA], callback[HTTP_MAX_CABECERA], callback_codificado[HTTP_MAX_CABECERA];
    char destino[HTTP_MAX_CABECERA];

    // Conserva el host y el puerto de la petición original para construir la URL de retorno.
    if(codificar(ruta, sizeof ruta, p->ruta) != 0 ||
       snprintf(callback, sizeof callback, "%s://%s%s?next=%s", p->tls ? "https" : "http", p->host, RUTA_CALLBACK,
                ruta) >= (int)sizeof callback ||
       codificar(callback_codificado, sizeof callback_codificado, callback) != 0 ||
       snprintf(destino, sizeof destino, "%s?url=%d&callback=%s", ui_login_url ? ui_login_url : "", url->id,
                callback_codificado) >= (int)sizeof destino) {
        http_respuesta_texto(salida, 414, "Ruta demasiado larga\n");
        return;
    }
    redirigir(salida, destino, NULL);
}

// Procesa el retorno del login, valida la sesion y establece la cookie antes de volver a la ruta solicitada
static int callback(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    const char *query = strchr(p->ruta, '?');
    char sesion[MAX_SESION + 1], siguiente[HTTP_MAX_CABECERA];
    if(!query || parametro(query + 1, "session", sesion, sizeof sesion) != 0 || !sesion_bien_formada(sesion)) {
        http_respuesta_texto(salida, 400, "Falta una sesion valida en el callback\n");
        return -1;
    }
    // Solo se permiten rutas locales como destino de retorno para evitar redirecciones externas
    if(parametro(query + 1, "next", siguiente, sizeof siguiente) != 0 || !ruta_local(siguiente))
        strcpy(siguiente, "/");

    int valida = sesion_valida(url->id, sesion);
    if(valida != 1) {
        if(valida == 0)
            http_respuesta_texto(salida, 401, "Sesion no valida\n");
        else
            http_respuesta_texto(salida, 503, "No se pudo verificar la sesion\n");
        return -1;
    }

    // La cookie queda limitada al host actual y no es accesible desde JavaScript
    // Secure se establece unicamente cuando la peticion original utiliza HTTPS
    char cookie[MAX_SESION + 64];
    snprintf(cookie, sizeof cookie, "%s=%s; Path=/; HttpOnly; SameSite=Lax%s", COOKIE_SESION, sesion,
             p->tls ? "; Secure" : "");
    redirigir(salida, siguiente, cookie);
    return -1;
}

static int usuario_password(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    size_t largo_callback = strlen(RUTA_CALLBACK);
    if(strncmp(p->ruta, RUTA_CALLBACK, largo_callback) == 0 &&
       (p->ruta[largo_callback] == '\0' || p->ruta[largo_callback] == '?'))
        return callback(url, p, salida);

    char sesion[MAX_SESION + 1];
    if(cookie_sesion(p, sesion, sizeof sesion) == 0 && sesion_bien_formada(sesion)) {
        int valida = sesion_valida(url->id, sesion);
        if(valida == 1)
            return 0;
        if(valida == -1) {
            http_respuesta_texto(salida, 503, "No se pudo verificar la sesion\n");
            return -1;
        }
    }
    // Sin cookie, o con una vencida o cerrada con logout, se vuelve a pedir el login
    mandar_al_login(url, p, salida);
    return -1;
}

// 0 si la petición pasa, -1 si ya dejó la respuesta de rechazo en salida
int auth_verificar(const UrlConfig *url, const Peticion *p, Respuesta *salida)
{
    if(strcmp(url->auth, "api_key") == 0)
        return x_api_key(url, p, salida);
    if(strcmp(url->auth, "user_password") == 0)
        return usuario_password(url, p, salida);
    return 0;
}
