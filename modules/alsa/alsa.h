#pragma once

#include "../../module.h"
#include "../../particle.h"

struct module *module_alsa(
    const char *card, const char *mixer, struct particle *label);
