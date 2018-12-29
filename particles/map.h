#pragma once
#include "../particle.h"

struct particle_map {
    const char *tag_value;
    struct particle *particle;
};

struct particle *particle_map_new(
    const char *tag, const struct particle_map *particle_map, size_t count,
    struct particle *default_particle, int left_margin, int right_margin);
