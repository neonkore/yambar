#pragma once

#include "module.h"
#include "particle.h"

const struct module_info *plugin_load_module(const char *name);
const struct particle_info *plugin_load_particle(const char *name);
