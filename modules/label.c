#include <stdlib.h>
#include <assert.h>

#include <poll.h>

#include "../config.h"
#include "../config-verify.h"
#include "../module.h"
#include "../plugin.h"

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

static const char *
description(const struct module *mod)
{
    return "label";
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;
    return m->label->instantiate(m->label, NULL);
}

static int
run(struct module *mod)
{
    return 0;
}

static struct module *
label_new(struct particle *label)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
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
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_label_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_label_iface")));
#endif
