// Adaptado de api.rs del P1: una sola llamada al contrato de resolve del P2.
#include "api.h"

#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESOLVE_PATH "/api/interceptor/resolve"
#define MAX_API_RESPONSE 16384

typedef struct {
    char data[MAX_API_RESPONSE + 1];
    size_t length;
} ApiResponse;

static size_t receive_body(char *data, size_t size, size_t count, void *context)
{
    ApiResponse *response = context;
    if(size && count > SIZE_MAX / size)
        return 0;
    size_t length = size * count;
    if(length > MAX_API_RESPONSE - response->length || memchr(data, '\0', length))
        return 0;
    memcpy(response->data + response->length, data, length);
    response->length += length;
    response->data[response->length] = '\0';
    return length;
}

static int parse_response(const ApiResponse *response, ResolveResult *result)
{
    cJSON *json = cJSON_ParseWithLengthOpts(response->data, response->length + 1, NULL, true);
    const cJSON *exists = cJSON_GetObjectItemCaseSensitive(json, "exists");
    int valid = cJSON_IsObject(json) && cJSON_IsBool(exists);
    result->exists = cJSON_IsTrue(exists);
    if(valid && result->exists) {
        const cJSON *address = cJSON_GetObjectItemCaseSensitive(json, "address");
        const cJSON *zone = cJSON_GetObjectItemCaseSensitive(json, "zone");
        valid = cJSON_IsString(zone) && zone->valuestring[0] &&
            cJSON_IsString(address) &&
            inet_pton(AF_INET, address->valuestring, &result->address) == 1;
    }
    cJSON_Delete(json);
    return valid ? 0 : -1;
}

int resolve_internal(const Config *config, const char *domain,
                     struct in_addr client_ip, ResolveResult *result)
{
    *result = (ResolveResult){0};
    CURL *curl = curl_easy_init();
    CURLU *url = curl_url();
    struct curl_slist *headers = NULL;
    char *escaped = NULL, *query = NULL, *header = NULL;
    char ip[INET_ADDRSTRLEN];
    int status = -1;
    if(!curl || !url || !inet_ntop(AF_INET, &client_ip, ip, sizeof ip))
        goto cleanup;
    escaped = curl_easy_escape(curl, domain, 0);
    if(!escaped)
        goto cleanup;
    size_t query_size = strlen(escaped) + strlen(ip) + sizeof "domain=&client_ip=";
    query = malloc(query_size);
    size_t header_size = strlen(config->node_api_key) + sizeof "x-api-key: ";
    header = malloc(header_size);
    if(!query || !header)
        goto cleanup;
    snprintf(query, query_size, "domain=%s&client_ip=%s", escaped, ip);
    snprintf(header, header_size, "x-api-key: %s", config->node_api_key);
    headers = curl_slist_append(NULL, header);
    if(!headers || curl_url_set(url, CURLUPART_URL, config->node_api_url, 0) != CURLUE_OK ||
       curl_url_set(url, CURLUPART_PATH, RESOLVE_PATH, 0) != CURLUE_OK ||
       curl_url_set(url, CURLUPART_QUERY, query, 0) != CURLUE_OK ||
       curl_url_set(url, CURLUPART_FRAGMENT, NULL, 0) != CURLUE_OK)
        goto cleanup;

    ApiResponse response = {0};
    long code = 0;
    curl_easy_setopt(curl, CURLOPT_CURLU, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)config->node_api_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)config->node_api_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    // No se siguen redirects ni se desactiva la validación TLS.
    CURLcode transfer = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(transfer != CURLE_OK) {
        fprintf(stderr, "No se pudo consultar el REST API: %s\n", curl_easy_strerror(transfer));
    } else if(code != 200) {
        fprintf(stderr, "El REST API respondió HTTP %ld\n", code);
    } else {
        status = parse_response(&response, result);
        if(status != 0)
            fprintf(stderr, "El REST API devolvió un JSON inválido para resolve\n");
    }

cleanup:
    free(header);
    free(query);
    curl_free(escaped);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_url_cleanup(url);
    return status;
}
