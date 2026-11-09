#include <criterion/criterion.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "fake_server.h"

static void entorno_valido(void)
{
    cr_assert_eq(setenv("CACHE_PORT", "8080", 1), 0);
    cr_assert_eq(setenv("CACHE_TLS_PORT", "8443", 1), 0);
    cr_assert_eq(setenv("TLS_CA_CERT_FILE", "/certs/rootCA.pem", 1), 0);
    cr_assert_eq(setenv("TLS_CA_KEY_FILE", "/certs/rootCA-key.pem", 1), 0);
    cr_assert_eq(setenv("NODE_API_URL", "http://api:8787", 1), 0);
    cr_assert_eq(setenv("NODE_API_KEY", "llave", 1), 0);
    cr_assert_eq(setenv("UI_LOGIN_URL", "https://ui.example.com/login", 1), 0);
    cr_assert_eq(setenv("CONFIG_REFRESH_SECONDS", "30", 1), 0);
    cr_assert_eq(setenv("PURGE_INTERVAL_SECONDS", "60", 1), 0);
    cr_assert_eq(setenv("CACHE_DIR", "/tmp/cache", 1), 0);
}

Test(config, carga_parametros_tls)
{
    entorno_valido();
    ConfigCache c;
    cr_assert_eq(config_cargar(&c), 0);
    cr_expect_eq(c.puerto, 8080);
    cr_expect_eq(c.puerto_tls, 8443);
    cr_expect_str_eq(c.ca_cert_file, "/certs/rootCA.pem");
    cr_expect_str_eq(c.ca_key_file, "/certs/rootCA-key.pem");
    cr_expect_str_eq(c.api_url, "http://api:8787");
    cr_expect_str_eq(c.api_key, "llave");
    cr_expect_str_eq(c.ui_login_url, "https://ui.example.com/login");
    cr_expect_eq(c.segundos_recarga, 30);

    setenv("CACHE_PORT", "1", 1);
    setenv("CACHE_TLS_PORT", "65535", 1);
    cr_expect_eq(config_cargar(&c), 0);
    setenv("CACHE_PORT", "65535", 1);
    setenv("CACHE_TLS_PORT", "1", 1);
    cr_expect_eq(config_cargar(&c), 0);
}

Test(config, rechaza_puertos_invalidos_o_iguales)
{
    const char *variables[] = {"CACHE_PORT", "CACHE_TLS_PORT"};
    const char *invalidos[] = {"0", "-1", "65536", "8443x", "8.5", "999999999999999999999"};
    ConfigCache c;
    for(size_t i = 0; i < sizeof variables / sizeof variables[0]; i++) {
        for(size_t j = 0; j < sizeof invalidos / sizeof invalidos[0]; j++) {
            entorno_valido();
            setenv(variables[i], invalidos[j], 1);
            cr_expect_eq(config_cargar(&c), -1, "%s=%s", variables[i], invalidos[j]);
        }
    }
    entorno_valido();
    setenv("CACHE_TLS_PORT", "8080", 1);
    cr_expect_eq(config_cargar(&c), -1);
}

Test(config, exige_variables_de_arranque)
{
    const char *variables[] = {"CACHE_PORT", "CACHE_TLS_PORT", "TLS_CA_CERT_FILE", "TLS_CA_KEY_FILE",
                               "NODE_API_URL", "NODE_API_KEY", "UI_LOGIN_URL", "CONFIG_REFRESH_SECONDS",
                               "PURGE_INTERVAL_SECONDS"};
    ConfigCache c;
    for(size_t i = 0; i < sizeof variables / sizeof variables[0]; i++) {
        entorno_valido();
        unsetenv(variables[i]);
        cr_expect_eq(config_cargar(&c), -1, "ausente: %s", variables[i]);
        setenv(variables[i], "", 1);
        cr_expect_eq(config_cargar(&c), -1, "vacía: %s", variables[i]);
    }
}

Test(config, ui_login_url_debe_ser_http)
{
    ConfigCache c;
    entorno_valido();
    setenv("UI_LOGIN_URL", "ftp://ui.example.com/login", 1);
    cr_expect_eq(config_cargar(&c), -1);
    setenv("UI_LOGIN_URL", "http://localhost:5173/login", 1);
    cr_expect_eq(config_cargar(&c), 0);
}

// Una URL del contrato con todo válido menos el id y el patrón, que cambian por prueba
#define URL(id, patron)                                                                              \
    "{\"id\": " #id ", \"pattern\": \"" patron "\", \"origin\": \"http://origen\", "                 \
    "\"tls_enabled\": false, \"cache_size_bytes\": 1000, \"replacement_policy\": \"lru\", \"auth\": \"none\", " \
    "\"file_types\": [{\"extension\": \"html\", \"ttl_seconds\": 60}]}"

// El id de la URL que le toca al host, 0 si no hay ninguna
static int buscar(const char *host)
{
    const UrlConfig *url = config_buscar(host);
    if(!url)
        return 0;
    int id = url->id;
    config_soltar(url);
    return id;
}

static void instalar(const char *json)
{
    Configuracion *c = config_parsear(json);
    cr_assert_not_null(c);
    config_instalar(c);
}

Test(config, exacta_gana_a_wildcard)
{
    instalar("{\"urls\": [" URL(1, "*.marca.com") "," URL(2, "www.marca.com") "," URL(3, "*.api.marca.com") "]}");
    cr_expect_eq(buscar("www.marca.com"), 2);
    cr_expect_eq(buscar("WWW.Marca.COM:8080"), 2);
    cr_expect_eq(buscar("img.marca.com"), 1);
    cr_expect_eq(buscar("a.b.marca.com"), 1);
    cr_expect_eq(buscar("IMG.MARCA.COM:8443"), 1);
    cr_expect_eq(buscar("v1.api.marca.com"), 3);
    cr_expect_eq(buscar("a.v1.api.marca.com"), 3);
    cr_expect_eq(buscar(".marca.com"), 0);
    cr_expect_eq(buscar("marca.com"), 1);
    cr_expect_eq(buscar("otramarca.com"), 0);
    cr_expect_eq(buscar("otra.com"), 0);
}

Test(config, sin_configuracion_todo_da_null)
{
    cr_expect_eq(buscar("www.marca.com"), 0);
}

Test(config, lee_los_campos_y_salta_las_invalidas)
{
    instalar("{\"urls\": [" URL(7, "api.marca.com") ", {\"id\": 8, \"pattern\": \"sin-origen.com\"}]}");
    const UrlConfig *url = config_buscar("api.marca.com");
    cr_assert_not_null(url);
    cr_expect_str_eq(url->origen, "http://origen");
    cr_expect_not(url->tls_enabled);
    cr_expect_eq(url->tamano_maximo, 1000);
    cr_expect_str_eq(url->politica, "lru");
    cr_expect_str_eq(url->auth, "none");
    cr_assert_eq(url->num_tipos, 1);
    cr_expect_str_eq(url->tipos[0].extension, "html");
    cr_expect_eq(url->tipos[0].ttl_segundos, 60);
    config_soltar(url);

    cr_expect_eq(buscar("sin-origen.com"), 0);
    cr_expect_null(config_parsear("no es json"));
    cr_expect_null(config_parsear("{\"otra\": []}"));
}

Test(config, tls_exige_booleano)
{
    cJSON *json = cJSON_Parse("{\"urls\": [" URL(7, "api.marca.com") "]}");
    cr_assert_not_null(json);
    cJSON *regla = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "urls"), 0);
    cJSON_ReplaceItemInObjectCaseSensitive(regla, "tls_enabled", cJSON_CreateTrue());
    char *texto = cJSON_PrintUnformatted(json);
    cr_assert_not_null(texto);
    instalar(texto);
    cJSON_free(texto);
    const UrlConfig *url = config_buscar("api.marca.com");
    cr_assert_not_null(url);
    cr_expect(url->tls_enabled);
    config_soltar(url);

    const char *invalidos[] = {"\"true\"", "1", "null", "[]", "{}"};
    for(size_t i = 0; i < sizeof invalidos / sizeof invalidos[0]; i++) {
        cJSON_ReplaceItemInObjectCaseSensitive(regla, "tls_enabled", cJSON_Parse(invalidos[i]));
        texto = cJSON_PrintUnformatted(json);
        cr_assert_not_null(texto);
        instalar(texto);
        cJSON_free(texto);
        cr_expect_eq(buscar("api.marca.com"), 0);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(regla, "tls_enabled");
    texto = cJSON_PrintUnformatted(json);
    cr_assert_not_null(texto);
    instalar(texto);
    cr_expect_eq(buscar("api.marca.com"), 0);
    cJSON_free(texto);
    cJSON_Delete(json);
}

Test(config, consulta_patron_tls_exacto_y_respeta_recargas)
{
    cr_expect_not(config_patron_tls_habilitado("*.marca.com"));
    // Activar TLS solo en la primera regla.
    cJSON *json = cJSON_Parse("{\"urls\": [" URL(1, "*.marca.com") "," URL(2, "api.marca.com") "]}");
    cr_assert_not_null(json);
    cJSON *regla = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "urls"), 0);
    cJSON_ReplaceItemInObjectCaseSensitive(regla, "tls_enabled", cJSON_CreateTrue());
    char *texto = cJSON_PrintUnformatted(json);
    cr_assert_not_null(texto);
    instalar(texto);
    cJSON_free(texto);
    cJSON_Delete(json);
    cr_expect(config_patron_tls_habilitado("*.marca.com"));
    cr_expect(config_patron_tls_habilitado("*.MARCA.COM"));
    cr_expect_not(config_patron_tls_habilitado("www.marca.com"));
    cr_expect_not(config_patron_tls_habilitado("api.marca.com"));
    cr_expect_not(config_patron_tls_habilitado("otro.com"));
    instalar("{\"urls\": []}");
    cr_expect_not(config_patron_tls_habilitado("*.marca.com"));
}

// Una petición que tiene la URL vieja la sigue pudiendo leer aunque ya se recargó
Test(config, recarga_sin_cortar)
{
    instalar("{\"urls\": [" URL(1, "www.marca.com") "]}");
    const UrlConfig *vieja = config_buscar("www.marca.com");
    cr_assert_not_null(vieja);
    instalar("{\"urls\": [" URL(2, "www.marca.com") "]}");
    cr_expect_eq(buscar("www.marca.com"), 2);
    cr_expect_str_eq(vieja->patron, "www.marca.com");
    config_soltar(vieja);
}

// Arranca la carga contra un API falso y devuelve la petición que le llegó
static void cargar_desde(fake_server *api, char *peticion, size_t tam)
{
    cr_assert_eq(fake_server_start(api), 0);
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", api->port);
    ConfigCache ajustes = {.api_url = url, .api_key = "llave-de-prueba", .segundos_recarga = 3600};
    curl_global_init(CURL_GLOBAL_DEFAULT);
    cr_assert_eq(config_iniciar(&ajustes), 0);
    fake_server_copy_last_request(api, peticion, tam);
    fake_server_stop(api);
}

Test(config, pide_la_configuracion_con_la_llave)
{
    fake_server api = {.speak_http = 1, .response_status = 200,
                       .response_body = "{\"urls\": [" URL(5, "www.marca.com") "]}"};
    char peticion[FAKE_SERVER_REQUEST_SIZE];
    cargar_desde(&api, peticion, sizeof peticion);
    cr_expect_eq(buscar("www.marca.com"), 5);
    cr_expect_not_null(strstr(peticion, "GET /api/cache/config HTTP/1.1\r\n"));
    cr_expect_not_null(strstr(peticion, "x-api-key: llave-de-prueba\r\n"));
}

Test(config, si_el_api_falla_se_queda_la_anterior)
{
    instalar("{\"urls\": [" URL(4, "www.marca.com") "]}");
    fake_server api = {.speak_http = 1, .response_status = 500};
    char peticion[FAKE_SERVER_REQUEST_SIZE];
    cargar_desde(&api, peticion, sizeof peticion);
    cr_expect_eq(buscar("www.marca.com"), 4);
}
