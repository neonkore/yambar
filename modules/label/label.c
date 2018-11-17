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
    free(mod);
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

    struct module *mod = malloc(sizeof(*mod));
    mod->bar = NULL;
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->begin_expose = &module_default_begin_expose;
    mod->expose = &module_default_expose;
    mod->end_expose = &module_default_end_expose;

    return mod;
}
