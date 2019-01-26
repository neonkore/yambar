#pragma once

#include "config.h"
#include "config-verify.h"
#include "module.h"
#include "particle.h"

typedef bool (*verify_func_t)(keychain_t *chain, const struct yml_node *node);

struct module_iface {
    verify_func_t verify_conf;
    struct module *(*from_conf)(
        const struct yml_node *node, struct conf_inherit inherited);
};

struct particle_iface {
    verify_func_t verify_conf;
    struct particle *(*from_conf)(
        const struct yml_node *node, struct particle *common);
};

struct deco_iface {
    verify_func_t verify_conf;
    struct deco *(*from_conf)(const struct yml_node *node);
};

const struct module_iface *plugin_load_module(const char *name);
const struct particle_iface *plugin_load_particle(const char *name);
const struct deco_iface *plugin_load_deco(const char *name);

enum plugin_type { PLUGIN_MODULE, PLUGIN_PARTICLE, PLUGIN_DECORATION };

struct plugin {
    char *name;
    enum plugin_type type;

    void *lib;
    union {
        const struct module_iface *module;
        const struct particle_iface *particle;
        const struct deco_iface *decoration;
        const void *dummy;

#if 0
        struct {
            void *sym1;
            void *sym2;
        } dummy;

        struct module_iface module;
        struct particle_iface particle;
        struct deco_iface decoration;
#endif
    };
};

const struct plugin *plugin_load(const char *name, enum plugin_type type);
