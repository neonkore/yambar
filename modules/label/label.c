#include <stdlib.h>
#include <assert.h>

#include <poll.h>

#include "../../config.h"

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
content(struct module *mod)
{
    const struct private *m = mod->private;
    return m->label->instantiate(m->label, NULL);
}

static int
run(struct module_run_context *ctx)
{
    module_signal_ready(ctx);
    return 0;
}

static struct module *
label_new(struct particle *label)
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

static struct module *
from_conf(const struct yml_node *node, const struct font *parent_font)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return label_new(conf_to_particle(c, parent_font));
}

const struct module_info module_info = {
    .from_conf = &from_conf,
    .attr_count = 2,
    .attrs = {
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        {NULL, false, NULL},
     },
};
