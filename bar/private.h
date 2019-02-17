#pragma once

#include <cairo.h>
#include <cairo-xcb.h>

#include "../bar/bar.h"
#include "backend.h"

struct private {
    /* From bar_config */
    char *monitor;
    enum bar_location location;
    int height;
    int left_spacing, right_spacing;
    int left_margin, right_margin;

    struct rgba background;

    struct {
        int width;
        struct rgba color;
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
    int x, y;
    int width;
    int height_with_border;

    /* Name of currently active cursor */
    char *cursor_name;

    cairo_t *cairo;
    cairo_surface_t *cairo_surface;

    struct {
        void *data;
        const struct backend *iface;
    } backend;
#if 0
    /* Backend specifics */
    xcb_connection_t *conn;

    xcb_window_t win;
    xcb_colormap_t colormap;
    xcb_pixmap_t pixmap;
    xcb_gc_t gc;
    xcb_cursor_context_t *cursor_ctx;
    xcb_cursor_t cursor;
#endif
};
