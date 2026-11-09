#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *extension;
    int ttl_segundos;
} TipoArchivo;

typedef struct Configuracion Configuracion;

// Una URL de GET /api/cache/config
typedef struct {
    int id;
    char *patron;
    bool tls_enabled;
    char *origen;
    size_t tamano_maximo;
    char *politica;
    char *auth;
    TipoArchivo *tipos;
    size_t num_tipos;
    Configuracion *dueno;
} UrlConfig;

typedef struct {
    int puerto;
    int puerto_tls;
    const char *ca_cert_file;
    const char *ca_key_file;
    const char *api_url;
    const char *api_key;
    const char *ui_login_url;
    int segundos_recarga;
    int segundos_purga;
    const char *directorio;
} ConfigCache;

int config_cargar(ConfigCache *config);
int config_iniciar(const ConfigCache *config);
const UrlConfig *config_buscar(const char *host);
void config_soltar(const UrlConfig *url);
// Consulta el patrón exacto de una regla, incluido "*." cuando corresponda.
bool config_patron_tls_habilitado(const char *patron);

// Públicas para las pruebas
Configuracion *config_parsear(const char *texto);
void config_instalar(Configuracion *nueva);

#endif
