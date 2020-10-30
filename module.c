#include "module.h"
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

struct module *
module_common_new(void)
{
    struct module *mod = calloc(1, sizeof(*mod));
    mtx_init(&mod->lock, mtx_plain);
    mod->destroy = &module_default_destroy;
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
    if (e != NULL)
        e->begin_expose(e);
    return e;
}
