#include "module.h"
#include <stdlib.h>

struct module *
module_common_new(void)
{
    struct module *mod = malloc(sizeof(*mod));
    mod->bar = NULL;
    mtx_init(&mod->lock, mtx_plain);
    mod->private = NULL;
    mod->run = NULL;
    mod->destroy = &module_default_destroy;
    mod->content = NULL;
    mod->begin_expose = &module_default_begin_expose;
    mod->expose = &module_default_expose;
    mod->end_expose = &module_default_end_expose;
    return mod;
}

void
module_default_destroy(struct module *mod)
{
    mtx_destroy(&mod->lock);
    free(mod);
}

struct module_expose_context
module_default_begin_expose(const struct module *mod, cairo_t *cr)
{
    struct exposable *e = mod->content(mod);
    return (struct module_expose_context){
        .exposable = e,
        .width = e->begin_expose(e, cr),
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
