#pragma once

#include "../bar/bar.h"
#include "backend.h"

struct private {
    /* From bar_config */
    char *monitor;
    enum bar_location location;
    int height;
    int left_spacing, right_spacing;
    int left_margin, right_margin;
    int trackpad_sensitivity;

    pixman_color_t background;

    struct {
        int left_width, right_width;
        int top_width, bottom_width;
        pixman_color_t color;
        int left_margin, right_margin;
        int top_margin, bottom_margin;
    } border;

    struct {
        struct module **mods;
        struct exposable **exps;
        size_t count;
    } left;
    struct {
        struct module **mods;
        struct exposable **exps;
        size_t count;
    } center;
    struct {
        struct module **mods;
        struct exposable **exps;
        size_t count;
    } right;

    /* Calculated run-time */
    int width;
    int height_with_border;

    pixman_image_t *pix;

    struct {
        void *data;
        const struct backend *iface;
    } backend;
};
