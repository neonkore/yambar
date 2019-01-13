#pragma once

#include "config-verify.h"
#include "module.h"
#include "particle.h"

struct module_iface {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
    struct module *(*from_conf)(
        const struct yml_node *node, struct conf_inherit inherited);
};

struct particle_iface {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
    struct particle *(*from_conf)(
        const struct yml_node *node, struct particle *common);
};

struct deco_iface {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
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
        struct {
            void *sym1;
            void *sym2;
        } dummy;

        struct module_iface module;
        struct particle_iface particle;
        struct deco_iface decoration;
    };
};

const struct plugin *plugin_load(const char *name, enum plugin_type type);
