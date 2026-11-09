#ifndef LOG_H
#define LOG_H

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level;

void log_message(log_level level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define log_info(...) log_message(LOG_INFO, __VA_ARGS__)
#define log_warn(...) log_message(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_message(LOG_ERROR, __VA_ARGS__)

#endif
