#pragma once

#include "color.h"
#include "module.h"

struct bar;
struct bar_run_context {
    struct bar *bar;
    int abort_fd;
};

struct bar {
    void *private;
    int (*run)(struct bar_run_context *ctx);
    void (*destroy)(struct bar *bar);

    void (*refresh)(const struct bar *bar);
    void (*set_cursor)(struct bar *bar, const char *cursor);
};

enum bar_location { BAR_TOP, BAR_BOTTOM };

struct bar_config {
    enum bar_location location;
    int height;
    int left_spacing, right_spacing;
    int left_margin, right_margin;

    struct rgba background;

    struct {
        int width;
        struct rgba color;
    } border;

    struct {
        struct module **mods;
        size_t count;
    } left;
    struct {
        struct module **mods;
        size_t count;
    } center;
    struct {
        struct module **mods;
        size_t count;
    } right;
};

struct bar *bar_new(const struct bar_config *config);
