#pragma once

#include <threads.h>

#include "particle.h"

struct bar;

struct module {
    const struct bar *bar;

    int abort_fd;
    mtx_t lock;

    void *private;

    int (*run)(struct module *mod);
    void (*destroy)(struct module *module);

    /*
     * Called by module_begin_expose(). Should return an
     * exposable (an instantiated particle).
     */
    struct exposable *(*content)(struct module *mod);

    /* refresh_in() should schedule a module content refresh after the
     * specified number of milliseconds */
    bool (*refresh_in)(struct module *mod, long milli_seconds);

    const char *(*description)(const struct module *mod);
};

struct module *module_common_new(void);
void module_default_destroy(struct module *mod);
struct exposable *module_begin_expose(struct module *mod);

/* List of attributes *all* modules implement */
#define MODULE_COMMON_ATTRS                        \
    {"content", true, &conf_verify_particle},      \
    {"anchors", false, NULL},                      \
    {"font", false, &conf_verify_font},            \
    {"foreground", false, &conf_verify_color},     \
    {NULL, false, NULL}
