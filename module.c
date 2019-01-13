#include "module.h"
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

struct module *
module_common_new(void)
{
    struct module *mod = malloc(sizeof(*mod));
    mod->bar = NULL;
    mtx_init(&mod->lock, mtx_plain);
    mod->private = NULL;

    mod->destroy = &module_default_destroy;
    mod->begin_expose = &module_default_begin_expose;
    mod->expose = &module_default_expose;
    mod->end_expose = &module_default_end_expose;

    /* No defaults for these; must be provided by implementation */
    mod->run = NULL;
    mod->content = NULL;
    mod->refresh_in = NULL;

    return mod;
}

void
module_default_destroy(struct module *mod)
{
    mtx_destroy(&mod->lock);
    free(mod);
}

void
module_signal_ready(struct module_run_context *ctx)
{
    write(ctx->ready_fd, &(uint64_t){1}, sizeof(uint64_t));
}

struct module_expose_context
module_default_begin_expose(struct module *mod)
{
    struct exposable *e = mod->content(mod);
    e->begin_expose(e);

    return (struct module_expose_context){
        .exposable = e,
        .private = NULL,
    };
}

void
module_default_expose(const struct module *mod,
                      const struct module_expose_context *ctx, cairo_t *cr,
                      int x, int y, int height)
{
    ctx->exposable->expose(ctx->exposable, cr, x, y, height);
}

void
module_default_end_expose(const struct module *mod,
                          struct module_expose_context *ctx)
{
    ctx->exposable->destroy(ctx->exposable);
}
