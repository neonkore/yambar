#pragma once
#include "../particle.h"

struct particle *particle_ramp_new(
    const char *tag, struct particle *particles[], size_t count,
    int left_margin, int right_margin, const char *on_click_template);
