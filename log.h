#pragma once

enum log_class { LOG_CLASS_ERROR, LOG_CLASS_WARNING, LOG_CLASS_INFO, LOG_CLASS_DEBUG };

void log_class(enum log_class log_class, const char *module,
               const char *file, int lineno,
               const char *fmt, ...) __attribute__((format (printf, 5, 6)));

#define LOG_ERR(fmt, ...)  \
    log_class(LOG_CLASS_ERROR, LOG_MODULE, __FILE__, __LINE__, fmt, ## __VA_ARGS__)
#define LOG_WARN(fmt, ...)  \
    log_class(LOG_CLASS_WARNING, LOG_MODULE, __FILE__, __LINE__, fmt, ## __VA_ARGS__)
#define LOG_INFO(fmt, ...)  \
    log_class(LOG_CLASS_INFO, LOG_MODULE, __FILE__, __LINE__, fmt, ## __VA_ARGS__)

#if defined(LOG_ENABLE_DBG) && LOG_ENABLE_DBG
 #define LOG_DBG(fmt, ...)  \
    log_class(LOG_CLASS_DEBUG, LOG_MODULE, __FILE__, __LINE__, fmt, ## __VA_ARGS__)
#else
 #define LOG_DBG(fmt, ...)
#endif
