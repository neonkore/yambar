#pragma once
#include "../particle.h"

struct particle *particle_empty_new(
    int left_margin, int right_margin, const char *on_click_template);
