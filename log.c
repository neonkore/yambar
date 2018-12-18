#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

static void
_log(enum log_class log_class, const char *module, const char *file, int lineno,
     const char *fmt, va_list va)
{
    bool colorize = true;

    const char *class;
    int class_clr;
    switch (log_class) {
    case LOG_CLASS_ERROR:    class = " err"; class_clr = 31; break;
    case LOG_CLASS_WARNING:  class = "warn"; class_clr = 33; break;
    case LOG_CLASS_INFO:     class = "info"; class_clr = 97; break;
    case LOG_CLASS_DEBUG:    class = " dbg"; class_clr = 36; break;
    }

    char clr[16];
    snprintf(clr, sizeof(clr), "\e[%dm", class_clr);
    printf("%s%s%s: ", colorize ? clr : "", class, colorize ? "\e[0m" : "");

    if (colorize)
        printf("\e[2m");
#if defined(_DEBUG)
    printf("%s:%d: ", file, lineno);
#else
    printf("%s: ", module);
#endif
    if (colorize)
        printf("\e[0m");

    //printf("%%s\n", buf);
    vprintf(fmt, va);
    printf("\n");
}

void
log_class(enum log_class log_class, const char *module,
          const char *file, int lineno, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _log(log_class, module, file, lineno, fmt, ap);
    va_end(ap);
}
