#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

static void
_log(enum log_class log_class, const char *module, const char *file, int lineno,
     const char *fmt, int sys_errno, va_list va)
{
    bool colorize = true;

    const char *class = "abcd";
    int class_clr = 0;
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

    if (sys_errno != 0)
        printf(": %s", strerror(sys_errno));

    printf("\n");
}

void
log_msg(enum log_class log_class, const char *module,
        const char *file, int lineno, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _log(log_class, module, file, lineno, fmt, 0, ap);
    va_end(ap);
}

void log_errno(enum log_class log_class, const char *module,
               const char *file, int lineno,
               const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _log(log_class, module, file, lineno, fmt, errno, ap);
    va_end(ap);
}

void log_errno_provided(enum log_class log_class, const char *module,
                        const char *file, int lineno, int _errno,
                        const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _log(log_class, module, file, lineno, fmt, _errno, ap);
    va_end(ap);
}
