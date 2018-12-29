#pragma once

#include <stdint.h>

#include "../module.h"
#include "../particle.h"

struct module *module_mpd(
    const char *host, uint16_t port, struct particle *label);
