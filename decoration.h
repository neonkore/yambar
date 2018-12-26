#pragma once

#include <cairo.h>

struct deco {
    void *private;
    void (*expose)(const struct deco *deco, cairo_t *cr,
                   int x, int y, int width, int height);
    void (*destroy)(struct deco *deco);
};
