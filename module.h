#pragma once

#include <cairo.h>

#include "particle.h"
#include "tag.h"

struct bar;
struct module;

struct module_run_context {
    struct module *module;
    int abort_fd;
};

struct module_expose_context {
    struct exposable *exposable;
    int width;
};

struct module {
    const struct bar *bar;

    void *private;

    int (*run)(struct module_run_context *ctx);
    void (*destroy)(struct module *module);

    struct exposable *(*content)(const struct module *mod);
    struct module_expose_context (*begin_expose)(const struct module *mod, cairo_t *cr);
    void (*expose)(const struct module *mod,
                   const struct module_expose_context *ctx,
                   cairo_t *cr, int x, int y, int height);
    void (*end_expose)(const struct module *mod, struct module_expose_context *ctx);
};

struct module_expose_context module_default_begin_expose(
    const struct module *mod, cairo_t *cr);

void module_default_expose(
    const struct module *mod,
    const struct module_expose_context *ctx, cairo_t *cr,
    int x, int y, int height);

void module_default_end_expose(
    const struct module *mod, struct module_expose_context *ctx);
