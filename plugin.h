#pragma once

#include "module.h"
#include "particle.h"

const struct module_info *plugin_load_module(const char *name);
const struct particle_info *plugin_load_particle(const char *name);

enum plugin_type { PLUGIN_MODULE, PLUGIN_PARTICLE };

struct plugin {
    char *name;
    enum plugin_type type;

    void *lib;
    const void *sym;
};

const struct plugin *plugin_load(const char *name, enum plugin_type type);
