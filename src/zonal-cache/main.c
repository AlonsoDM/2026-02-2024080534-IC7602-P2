#include <curl/curl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "cache.h"
#include "auth.h"
#include "config.h"
#include "servidor.h"
#include "tls.h"

// Carga la configuración y arranca el servidor, que solo regresa si falla
int main(void)
{
    ConfigCache config;
    // OpenSSL también escribe al socket, sin pasar por send con MSG_NOSIGNAL
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return EXIT_FAILURE;
    if(config_cargar(&config) != 0 || tls_iniciar(config.ca_cert_file, config.ca_key_file) != 0)
        return EXIT_FAILURE;
    auth_iniciar(&config);
    // curl_global_init no es seguro con hilos, por eso va antes de crear cualquiera
    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK ||
       cache_iniciar(config.directorio, config.segundos_purga) != 0 || config_iniciar(&config) != 0) {
        tls_liberar();
        return EXIT_FAILURE;
    }
    int servidor = servidor_abrir(config.puerto);
    if(servidor < 0) {
        tls_liberar();
        return EXIT_FAILURE;
    }
    int servidor_tls = servidor_abrir(config.puerto_tls);
    if(servidor_tls < 0) {
        close(servidor);
        tls_liberar();
        return EXIT_FAILURE;
    }
    servidor_atender(servidor, servidor_tls);
    close(servidor);
    close(servidor_tls);
    // Puede haber hilos de clientes usando TLS; el proceso termina sin liberar su estado en uso.
    return EXIT_FAILURE;
}
