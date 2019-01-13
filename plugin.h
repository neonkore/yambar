#pragma once

#include "config-verify.h"
#include "module.h"
#include "particle.h"

struct particle_iface {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
    struct particle *(*from_conf)(
        const struct yml_node *node, struct particle *common);
};

const struct module_info *plugin_load_module(const char *name);
const struct particle_iface *plugin_load_particle(const char *name);

enum plugin_type { PLUGIN_MODULE, PLUGIN_PARTICLE };

struct plugin {
    char *name;
    enum plugin_type type;

    void *lib;
    union {
        void *sym;
        struct particle_iface particle;
    };
};

const struct plugin *plugin_load(const char *name, enum plugin_type type);
