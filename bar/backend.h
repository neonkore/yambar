#pragma once

#include <stdbool.h>

#include "bar.h"

struct backend {
    bool (*setup)(struct bar *bar);
    void (*cleanup)(struct bar *bar);
    void (*loop)(struct bar *bar,
                 void (*expose)(const struct bar *bar),
                 void (*on_mouse)(struct bar *bar, enum mouse_event event,
                                  int x, int y));
    pixman_image_t *(*get_pixman_image)(const struct bar *bar);
    void (*commit_pixman)(const struct bar *bar, pixman_image_t *pix);
    void (*refresh)(const struct bar *bar);
    void (*set_cursor)(struct bar *bar, const char *cursor);
};
