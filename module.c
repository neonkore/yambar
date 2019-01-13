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

struct exposable *
module_begin_expose(struct module *mod)
{
    struct exposable *e = mod->content(mod);
    e->begin_expose(e);
    return e;
}
