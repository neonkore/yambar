#pragma once

#include <pixman.h>

struct deco {
    void *private;
    void (*expose)(const struct deco *deco, pixman_image_t *pix,
                   int x, int y, int width, int height);
    void (*destroy)(struct deco *deco);
};

#define DECORATION_COMMON_ATTRS  \
    {NULL, false, NULL}
