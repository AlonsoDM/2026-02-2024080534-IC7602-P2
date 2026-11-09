#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static const char *level_name(log_level level) {
    switch(level) {
        case LOG_INFO:
            return "INFO";
        case LOG_WARN:
            return "WARN";
        case LOG_ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

// Función variádica (acepta dinámicamente argumentos) para formatear logs dinámicos y enviarlos
void log_message(log_level level, const char *format, ...) {
    time_t now = time(NULL);
    struct tm utc_time;
    char timestamp[32] = "";

    // Usar timestamps UTC 
    if(gmtime_r(&now, &utc_time) != NULL) {
        strftime(timestamp, sizeof timestamp, "%Y-%m-%dT%H:%M:%SZ", &utc_time);
    }

    FILE *stream;
    if(level == LOG_ERROR) {
        stream = stderr;
    } else { stream = stdout; }

    
    fprintf(stream, "%s [%s] ", timestamp, level_name(level));

    // Redirijo los argumentos de la función variádica a vfprintf para imprimir el msg del log
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stream, format, arguments);
    va_end(arguments);

    fputc('\n', stream);
    fflush(stream); // Mando inmediatamente el log
}
