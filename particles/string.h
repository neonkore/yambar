#pragma once
#include "../particle.h"

struct particle *particle_string_new(
    const char *text, size_t max_len, struct font *font, struct rgba foreground,
    int left_margin, int right_margin, const char *on_click_template);
