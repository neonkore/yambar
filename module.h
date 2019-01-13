#pragma once

#include <threads.h>
#include <cairo.h>

#include "config.h"
#include "particle.h"
#include "tag.h"
#include "yml.h"

struct bar;
struct module;

struct module_info {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
    struct module *(*from_conf)(
        const struct yml_node *node, struct conf_inherit inherited);

#define MODULE_COMMON_ATTRS                        \
    {"font", false, &conf_verify_font},            \
    {"foreground", false, &conf_verify_color},     \
    {NULL, false, NULL}
};

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
};

struct module *module_common_new(void);
void module_default_destroy(struct module *mod);
struct exposable *module_begin_expose(struct module *mod);
