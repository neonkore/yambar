#include <stdlib.h>
#include <assert.h>

#include <poll.h>

#include "../config.h"
#include "../module.h"

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
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return label_new(conf_to_particle(c, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_info plugin_info = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};
