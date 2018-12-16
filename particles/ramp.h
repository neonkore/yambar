#pragma once
#include "../particle.h"

struct particle *particle_ramp_new(
    const char *tag, struct particle *particles[], size_t count);
