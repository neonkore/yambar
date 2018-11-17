#pragma once
#include "../particle.h"

struct particle *particle_string_new(
    const char *text, struct font *font, struct rgba foreground,
    int left_margin, int right_margin);
