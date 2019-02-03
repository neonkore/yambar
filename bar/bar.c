#include "../bar.h"
#include "private.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <threads.h>
#include <assert.h>

#include <sys/eventfd.h>

#define LOG_MODULE "bar"
#define LOG_ENABLE_DBG 0
#include "../log.h"

#include "xcb.h"
#include "wayland.h"

/*
 * Calculate total width of left/center/rigth groups.
 * Note: begin_expose() must have been called
 */
static void
calculate_widths(const struct private *b, int *left, int *center, int *right)
{
    *left = 0;
    *center = 0;
    *right = 0;

    for (size_t i = 0; i < b->left.count; i++) {
        struct exposable *e = b->left.exps[i];
        assert(e != NULL);
        *left += b->left_spacing + e->width + b->right_spacing;
    }

    for (size_t i = 0; i < b->center.count; i++) {
        struct exposable *e = b->center.exps[i];
        assert(e != NULL);
        *center += b->left_spacing + e->width + b->right_spacing;
    }

    for (size_t i = 0; i < b->right.count; i++) {
        struct exposable *e = b->right.exps[i];
        assert(e != NULL);
        *right += b->left_spacing + e->width + b->right_spacing;
    }

    /* No spacing on the edges (that's what the margins are for) */
    *left -= b->left_spacing + b->right_spacing;
    *center -= b->left_spacing + b->right_spacing;
    *right -= b->left_spacing + b->right_spacing;
}

static void
expose(const struct bar *_bar)
{
    const struct private *bar = _bar->private;

    double r, g, b, a;
    r = bar->background.red;
    g = bar->background.green;
    b = bar->background.blue;
    a = bar->background.alpha;

    cairo_set_source_rgba(bar->cairo, r, g, b, a);
    cairo_set_operator(bar->cairo, CAIRO_OPERATOR_SOURCE);
    cairo_paint(bar->cairo);

    if (bar->border.width > 0) {
        /* TODO: actually use border width */
        r = bar->border.color.red;
        g = bar->border.color.green;
        b = bar->border.color.blue;
        a = bar->border.color.alpha;

        cairo_set_line_width(bar->cairo, bar->border.width);
        cairo_set_source_rgba(bar->cairo, r, g, b, a);
        cairo_set_operator(bar->cairo, CAIRO_OPERATOR_OVER);
        cairo_rectangle(bar->cairo, 0, 0, bar->width, bar->height_with_border);
        cairo_stroke(bar->cairo);
    }

    for (size_t i = 0; i < bar->left.count; i++) {
        struct module *m = bar->left.mods[i];
        struct exposable *e = bar->left.exps[i];

        if (e != NULL)
            e->destroy(e);

        bar->left.exps[i] = module_begin_expose(m);
    }

    for (size_t i = 0; i < bar->center.count; i++) {
        struct module *m = bar->center.mods[i];
        struct exposable *e = bar->center.exps[i];

        if (e != NULL)
            e->destroy(e);

        bar->center.exps[i] = module_begin_expose(m);
    }

    for (size_t i = 0; i < bar->right.count; i++) {
        struct module *m = bar->right.mods[i];
        struct exposable *e = bar->right.exps[i];

        if (e != NULL)
            e->destroy(e);

        bar->right.exps[i] = module_begin_expose(m);
    }

    int left_width, center_width, right_width;
    calculate_widths(bar, &left_width, &center_width, &right_width);

    int y = bar->border.width;
    int x = bar->border.width + bar->left_margin - bar->left_spacing;
    for (size_t i = 0; i < bar->left.count; i++) {
        const struct exposable *e = bar->left.exps[i];
        e->expose(e, bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + e->width + bar->right_spacing;
    }

    x = bar->width / 2 - center_width / 2 - bar->left_spacing;
    for (size_t i = 0; i < bar->center.count; i++) {
        const struct exposable *e = bar->center.exps[i];
        e->expose(e, bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + e->width + bar->right_spacing;
    }

    x = bar->width - (
        right_width +
        bar->left_spacing +
        bar->right_margin +
        bar->border.width);

    for (size_t i = 0; i < bar->right.count; i++) {
        const struct exposable *e = bar->right.exps[i];
        e->expose(e, bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + e->width + bar->right_spacing;
    }

    cairo_surface_flush(bar->cairo_surface);
    bar->backend.iface->commit_surface(_bar);
}


static void
refresh(const struct bar *bar)
{
    const struct private *b = bar->private;
    b->backend.iface->refresh(bar);
}

static void
set_cursor(struct bar *bar, const char *cursor)
{
    struct private *b = bar->private;

    if (b->cursor_name != NULL && strcmp(b->cursor_name, cursor) == 0)
        return;

    free(b->cursor_name);
    b->cursor_name = strdup(cursor);

    b->backend.iface->set_cursor(bar, cursor);
}

static void
on_mouse(struct bar *_bar, enum mouse_event event, int x, int y)
{
    struct private *bar = _bar->private;

    if ((y < bar->border.width ||
         y >= (bar->height_with_border - bar->border.width)) ||
        (x < bar->border.width || x >= (bar->width - bar->border.width)))
    {
        set_cursor(_bar, "left_ptr");
        return;
    }

    int left_width, center_width, right_width;
    calculate_widths(bar, &left_width, &center_width, &right_width);

    int mx = bar->border.width + bar->left_margin - bar->left_spacing;
    for (size_t i = 0; i < bar->left.count; i++) {
        struct exposable *e = bar->left.exps[i];

        mx += bar->left_spacing;
        if (x >= mx && x < mx + e->width) {
            if (e->on_mouse != NULL)
                e->on_mouse(e, _bar, event, x - mx, y);
            return;
        }

        mx += e->width + bar->right_spacing;
    }

    mx = bar->width / 2 - center_width / 2 - bar->left_spacing;
    for (size_t i = 0; i < bar->center.count; i++) {
        struct exposable *e = bar->center.exps[i];

        mx += bar->left_spacing;
        if (x >= mx && x < mx + e->width) {
            if (e->on_mouse != NULL)
                e->on_mouse(e, _bar, event, x - mx, y);
            return;
        }

        mx += e->width + bar->right_spacing;
    }

    mx = bar->width - (right_width
                       + bar->left_spacing +
                       bar->right_margin +
                       bar->border.width);

    for (size_t i = 0; i < bar->right.count; i++) {
        struct exposable *e = bar->right.exps[i];

        mx += bar->left_spacing;
        if (x >= mx && x < mx + e->width) {
            if (e->on_mouse != NULL)
                e->on_mouse(e, _bar, event, x - mx, y);
            return;
        }

        mx += e->width + bar->right_spacing;
    }

    set_cursor(_bar, "left_ptr");
}


static int
run(struct bar *_bar)
{
    struct private *bar = _bar->private;

    bar->height_with_border = bar->height + 2 * bar->border.width;

    if (!bar->backend.iface->setup(_bar))
        return 1;

    set_cursor(_bar, "left_ptr");

    /* Start modules */
    thrd_t thrd_left[bar->left.count];
    thrd_t thrd_center[bar->center.count];
    thrd_t thrd_right[bar->right.count];

    for (size_t i = 0; i < bar->left.count; i++) {
        struct module *mod = bar->left.mods[i];

        mod->abort_fd = _bar->abort_fd;
        thrd_create(&thrd_left[i], (int (*)(void *))bar->left.mods[i]->run, mod);
    }
    for (size_t i = 0; i < bar->center.count; i++) {
        struct module *mod = bar->center.mods[i];

        mod->abort_fd = _bar->abort_fd;
        thrd_create(&thrd_center[i], (int (*)(void *))bar->center.mods[i]->run, mod);
    }
    for (size_t i = 0; i < bar->right.count; i++) {
        struct module *mod = bar->right.mods[i];

        mod->abort_fd = _bar->abort_fd;
        thrd_create(&thrd_right[i], (int (*)(void *))bar->right.mods[i]->run, mod);
    }

    LOG_DBG("all modules started");

    bar->backend.iface->loop(_bar, &expose, &on_mouse);

    LOG_DBG("shutting down");

    /* Wait for modules to terminate */
    int ret = 0;
    int mod_ret;
    for (size_t i = 0; i < bar->left.count; i++) {
        thrd_join(thrd_left[i], &mod_ret);
        if (mod_ret != 0)
            LOG_ERR("module: LEFT #%zu: non-zero exit value: %d", i, mod_ret);
        ret = ret == 0 && mod_ret != 0 ? mod_ret : ret;
    }
    for (size_t i = 0; i < bar->center.count; i++) {
        thrd_join(thrd_center[i], &mod_ret);
        if (mod_ret != 0)
            LOG_ERR("module: CENTER #%zu: non-zero exit value: %d", i, mod_ret);
        ret = ret == 0 && mod_ret != 0 ? mod_ret : ret;
    }
    for (size_t i = 0; i < bar->right.count; i++) {
        thrd_join(thrd_right[i], &mod_ret);
        if (mod_ret != 0)
            LOG_ERR("module: RIGHT #%zu: non-zero exit value: %d", i, mod_ret);
        ret = ret == 0 && mod_ret != 0 ? mod_ret : ret;
    }

    LOG_DBG("modules joined");

    for (size_t i = 0; i < bar->left.count; i++) {
        struct module *m = bar->left.mods[i];
        struct exposable *e = bar->left.exps[i];

        if (e != NULL)
            e->destroy(e);
        m->destroy(m);
    }
    for (size_t i = 0; i < bar->center.count; i++) {
        struct module *m = bar->center.mods[i];
        struct exposable *e = bar->center.exps[i];

        if (e != NULL)
            e->destroy(e);
        m->destroy(m);
    }
    for (size_t i = 0; i < bar->right.count; i++) {
        struct module *m = bar->right.mods[i];
        struct exposable *e = bar->right.exps[i];

        if (e != NULL)
            e->destroy(e);
        m->destroy(m);
    }

    bar->backend.iface->cleanup(_bar);

    if (bar->cairo)
        cairo_destroy(bar->cairo);
    if (bar->cairo_surface) {
        cairo_device_finish(cairo_surface_get_device(bar->cairo_surface));
        cairo_surface_finish(bar->cairo_surface);
        cairo_surface_destroy(bar->cairo_surface);
    }
    cairo_debug_reset_static_data();

    LOG_DBG("bar exiting");
    return ret;
}

static void
destroy(struct bar *bar)
{
    struct private *b = bar->private;

    free(b->left.mods);
    free(b->left.exps);
    free(b->center.mods);
    free(b->center.exps);
    free(b->right.mods);
    free(b->right.exps);
    free(b->monitor);
    free(b->backend.data);

    free(bar->private);
    free(bar);
}

struct bar *
bar_new(const struct bar_config *config)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->monitor = config->monitor != NULL ? strdup(config->monitor) : NULL;
    priv->location = config->location;
    priv->height = config->height;
    priv->background = config->background;
    priv->left_spacing = config->left_spacing;
    priv->right_spacing = config->right_spacing;
    priv->left_margin = config->left_margin;
    priv->right_margin = config->right_margin;
    priv->border.width = config->border.width;
    priv->border.color = config->border.color;
    priv->left.mods = malloc(config->left.count * sizeof(priv->left.mods[0]));
    priv->left.exps = malloc(config->left.count * sizeof(priv->left.exps[0]));
    priv->center.mods = malloc(config->center.count * sizeof(priv->center.mods[0]));
    priv->center.exps = malloc(config->center.count * sizeof(priv->center.exps[0]));
    priv->right.mods = malloc(config->right.count * sizeof(priv->right.mods[0]));
    priv->right.exps = malloc(config->right.count * sizeof(priv->right.exps[0]));
    priv->left.count = config->left.count;
    priv->center.count = config->center.count;
    priv->right.count = config->right.count;
    priv->cursor_name = NULL;

#if 0
    priv->backend.data = bar_backend_xcb_new();
    priv->backend.iface = &xcb_backend_iface;
#else
    priv->backend.data = bar_backend_wayland_new();
    priv->backend.iface = &wayland_backend_iface;
#endif

    for (size_t i = 0; i < priv->left.count; i++) {
        priv->left.mods[i] = config->left.mods[i];
        priv->left.exps[i] = NULL;
    }
    for (size_t i = 0; i < priv->center.count; i++) {
        priv->center.mods[i] = config->center.mods[i];
        priv->center.exps[i] = NULL;
    }
    for (size_t i = 0; i < priv->right.count; i++) {
        priv->right.mods[i] = config->right.mods[i];
        priv->right.exps[i] = NULL;
    }

    struct bar *bar = malloc(sizeof(*bar));
    bar->private = priv;
    bar->run = &run;
    bar->destroy = &destroy;
    bar->refresh = &refresh;
    bar->set_cursor = &set_cursor;

    for (size_t i = 0; i < priv->left.count; i++)
        priv->left.mods[i]->bar = bar;
    for (size_t i = 0; i < priv->center.count; i++)
        priv->center.mods[i]->bar = bar;
    for (size_t i = 0; i < priv->right.count; i++)
        priv->right.mods[i]->bar = bar;

    return bar;
}
