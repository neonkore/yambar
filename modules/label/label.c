#include "label.h"

#include <stdlib.h>
#include <assert.h>

#include <poll.h>

struct private {
    struct particle *label;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(const struct module *mod)
{
    const struct private *m = mod->private;
    return m->label->instantiate(m->label, NULL);
}

static int
run(struct module_run_context *ctx)
{
    return 0;
}

struct module *
module_label(struct particle *label)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
