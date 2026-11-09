#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "server.h"

int main(void)
{
    // Se inicializa libcurl antes de crear los hilos.
    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "No se pudo inicializar libcurl\n");
        return EXIT_FAILURE;
    }
    Config config;
    if(config_from_env(&config) != 0) {
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    // La configuración vive mientras corre el servidor.
    int status = server_run(&config);
    curl_global_cleanup();
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
