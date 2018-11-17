#include "module.h"
#include <stdlib.h>

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
