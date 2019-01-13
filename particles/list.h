#pragma once
#include "../particle.h"

struct particle *particle_list_new(
    struct particle *common,
    struct particle *particles[], size_t count,
    int left_spacing, int right_spacing);

extern const struct particle_info particle_list;
