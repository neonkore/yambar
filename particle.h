#pragma once

#include <cairo.h>
#include "color.h"
#include "font.h"
#include "tag.h"

struct exposable;

struct particle {
    struct particle *parent;
    void *private;

    int left_margin, right_margin;

    void (*destroy)(struct particle *particle);
    struct exposable *(*instantiate)(const struct particle *particle,
                                     const struct tag_set *tags);
};


struct exposable {
    const struct particle *particle;
    void *private;

    void (*destroy)(struct exposable *exposable);
    int (*begin_expose)(const struct exposable *exposable, cairo_t *cr);
    void (*expose)(const struct exposable *exposable, cairo_t *cr,
                   int x, int y, int height);
};

struct particle *particle_common_new(int left_margin, int right_margin);
