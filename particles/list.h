#pragma once
#include "../particle.h"

struct particle *particle_list_new(
    struct particle *particles[], size_t count,
    int left_spacing, int right_spacing, int left_margin, int right_margin);
