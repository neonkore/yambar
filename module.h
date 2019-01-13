#pragma once

#include <threads.h>
#include <cairo.h>

#include "config-verify.h"
#include "particle.h"
#include "tag.h"
#include "yml.h"

struct bar;
struct module;

struct module_info {
    struct module *(*from_conf)(const struct yml_node *node,
                               const struct font *parent_font);
    const struct attr_info attrs[];
};

struct module_run_context {
    struct module *module;
    int ready_fd;
    int abort_fd;
};

struct module_expose_context {
    struct exposable *exposable;
    int width;
    void *private;
};

struct module {
    const struct bar *bar;
    mtx_t lock;

    void *private;

    int (*run)(struct module_run_context *ctx);
    void (*destroy)(struct module *module);

    /*
     * Called by module_default_begin_expose(). Should return an
     * exposable (an instantiated particle).
     *
     * You may also choose to implement begin_expose(), expose() and
     * end_expose() yourself, in which case you do *not* have to
     * implement content().
     */
    struct exposable *(*content)(struct module *mod);

    /* refresh_in() should schedule a module content refresh after the
     * specified number of milliseconds */
    bool (*refresh_in)(struct module *mod, long milli_seconds);

    /*
     * Called by bar when it needs to refresh
     *
     * begin_expose() should return a module_expose_context, where the
     * 'exposable' member is an instantiated particle, 'width' is the
     * total width of the module, and 'private' is context data for
     * the module (i.e. it's not touched by the bar).
     *
     * expose() should render the exposable
     *
     * end_expose() performs cleanup (destroy exposable etc)
     *
     * Note that for most modules, using the default implementations
     * (module_default_*) is good enough. In this case, implement
     * 'content()' instead (see above).
     */
    struct module_expose_context (*begin_expose)(struct module *mod);
    void (*expose)(const struct module *mod,
                   const struct module_expose_context *ctx,
                   cairo_t *cr, int x, int y, int height);
    void (*end_expose)(const struct module *mod, struct module_expose_context *ctx);

};

struct module *module_common_new(void);
void module_signal_ready(struct module_run_context *ctx);

void module_default_destroy(struct module *mod);

struct module_expose_context module_default_begin_expose(
    struct module *mod);

void module_default_expose(
    const struct module *mod,
    const struct module_expose_context *ctx, cairo_t *cr,
    int x, int y, int height);

void module_default_end_expose(
    const struct module *mod, struct module_expose_context *ctx);
