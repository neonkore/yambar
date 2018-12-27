#pragma once
#include "../particle.h"

struct particle * particle_progress_bar_new(
    const char *tag, int width,
    struct particle *start_marker, struct particle *end_marker,
    struct particle *fill, struct particle *empty, struct particle *indicator,
    int left_margin, int right_margin);
