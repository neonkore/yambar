#include "bar.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <threads.h>
#include <assert.h>
#include <unistd.h>

#include <poll.h>
#include <signal.h>

#include <sys/eventfd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/randr.h>
#include <xcb/render.h>
#include <xcb/xcb_ewmh.h>

#include <cairo.h>
#include <cairo-xcb.h>

#define LOG_MODULE "bar"
#include "log.h"
#include "xcb.h"

struct private {
    /* From bar_config */
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

    /* Calculated run-time */
    int x, y;
    int width;
    int height_with_border;

    /* Resources */
    xcb_connection_t *conn;

    xcb_window_t win;
    xcb_colormap_t colormap;
    xcb_pixmap_t pixmap;
    xcb_gc_t gc;

    cairo_t *cairo;
};

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

    int left_width = 0;
    int center_width = 0;
    int right_width = 0;

    struct module_expose_context ctx_left[bar->left.count];
    for (size_t i = 0; i < bar->left.count; i++) {
        struct module *m = bar->left.mods[i];
        ctx_left[i] = m->begin_expose(m, bar->cairo);
        left_width += bar->left_spacing + ctx_left[i].width + bar->right_spacing;
    }

    struct module_expose_context ctx_center[bar->center.count];
    for (size_t i = 0; i < bar->center.count; i++) {
        struct module *m = bar->center.mods[i];
        ctx_center[i] = m->begin_expose(m, bar->cairo);
        center_width += bar->left_spacing + ctx_center[i].width + bar->right_spacing;
    }

    struct module_expose_context ctx_right[bar->right.count];
    for (size_t i = 0; i < bar->right.count; i++) {
        struct module *m = bar->right.mods[i];
        ctx_right[i] = m->begin_expose(m, bar->cairo);
        right_width += bar->left_spacing + ctx_right[i].width + bar->right_spacing;
    }

    /* No spacing on the edges (that's what the margins are for) */
    left_width -= bar->left_spacing + bar->right_spacing;
    center_width -= bar->left_spacing + bar->right_spacing;
    right_width -= bar->left_spacing + bar->right_spacing;

    int y = bar->border.width;
    int x = bar->border.width + bar->left_margin - bar->left_spacing;
    for (size_t i = 0; i < bar->left.count; i++) {
        const struct module *m = bar->left.mods[i];
        m->expose(m, &ctx_left[i], bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + ctx_left[i].width + bar->right_spacing;
    }

    x = bar->width / 2 - center_width / 2 - bar->left_spacing;
    for (size_t i = 0; i < bar->center.count; i++) {
        const struct module *m = bar->center.mods[i];
        m->expose(m, &ctx_center[i], bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + ctx_center[i].width + bar->right_spacing;
    }

    x = bar->width - (
        right_width +
        bar->left_spacing +
        bar->right_margin +
        bar->border.width);

    for (size_t i = 0; i < bar->right.count; i++) {
        const struct module *m = bar->right.mods[i];
        m->expose(m, &ctx_right[i], bar->cairo, x + bar->left_spacing, y, bar->height);
        x += bar->left_spacing + ctx_right[i].width + bar->right_spacing;
    }

    xcb_copy_area(bar->conn, bar->pixmap, bar->win, bar->gc,
                  0, 0, 0, 0, bar->width, bar->height_with_border);

    for (size_t i = 0; i < bar->left.count; i++) {
        const struct module *m = bar->left.mods[i];
        m->end_expose(m, &ctx_left[i]);
    }
    for (size_t i = 0; i < bar->center.count; i++) {
        const struct module *m = bar->center.mods[i];
        m->end_expose(m, &ctx_center[i]);
    }
    for (size_t i = 0; i < bar->right.count; i++) {
        const struct module *m = bar->right.mods[i];
        m->end_expose(m, &ctx_right[i]);
    }
}

static void refresh(const struct bar *bar);

static int
run(struct bar_run_context *run_ctx)
{
    struct bar *_bar = run_ctx->bar;
    struct private *bar = _bar->private;

    /* TODO: a lot of this (up to mapping the window) could be done in bar_new() */
    xcb_generic_error_t *e;

    bar->conn = xcb_connect(NULL, NULL);
    assert(bar->conn != NULL);

    const xcb_setup_t *setup = xcb_get_setup(bar->conn);

    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;

    xcb_randr_get_monitors_reply_t *monitors = xcb_randr_get_monitors_reply(
        bar->conn,
        xcb_randr_get_monitors(bar->conn, screen->root, 0),
        &e);
    assert(e == NULL);

    bar->height_with_border = bar->height + 2 * bar->border.width;

    /* Find monitor coordinates and width/height */
    for (xcb_randr_monitor_info_iterator_t it =
             xcb_randr_get_monitors_monitors_iterator(monitors);
         it.rem > 0;
         xcb_randr_monitor_info_next(&it))
    {
        const xcb_randr_monitor_info_t *mon = it.data;
        char *name = get_atom_name(bar->conn, mon->name);

        LOG_INFO("monitor: %s: %ux%u+%u+%u (%ux%umm)", name,
               mon->width, mon->height, mon->x, mon->y,
               mon->width_in_millimeters, mon->height_in_millimeters);

        free(name);

        if (!mon->primary)
            continue;

        bar->x = mon->x;
        bar->y = mon->y;
        bar->width = mon->width;
        bar->y += bar->location == BAR_TOP ? 0
            : screen->height_in_pixels - bar->height_with_border;
        break;
    }
    free(monitors);

    /* Find a 32-bit visual (TODO: fallback to 24-bit) */
    const uint8_t wanted_depth = 32;
    const uint8_t wanted_class = 0;

    uint8_t depth = 0;
    xcb_visualtype_t *vis = NULL;

    for (xcb_depth_iterator_t it = xcb_screen_allowed_depths_iterator(screen);
         it.rem > 0;
         xcb_depth_next(&it))
    {
        const xcb_depth_t *_depth = it.data;

        if (!(wanted_depth == 0 || _depth->depth == wanted_depth))
            continue;

        for (xcb_visualtype_iterator_t vis_it =
                 xcb_depth_visuals_iterator(_depth);
             vis_it.rem > 0;
             xcb_visualtype_next(&vis_it))
        {
            xcb_visualtype_t *_vis = vis_it.data;
            if (!(wanted_class == 0 || _vis->_class == wanted_class))
                continue;

            vis = _vis;
            break;
        }

        if (vis != NULL) {
            depth = _depth->depth;
            break;
        }
    }

    assert(depth == 32 || depth == 24);
    assert(vis != NULL);

    bar->colormap = xcb_generate_id(bar->conn);
    xcb_create_colormap(bar->conn, 0, bar->colormap, screen->root, vis->visual_id);

    bar->win = xcb_generate_id(bar->conn);
    xcb_create_window(
        bar->conn,
        depth, bar->win, screen->root,
        bar->x, bar->y, bar->width, bar->height_with_border,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, vis->visual_id,
        (XCB_CW_BACK_PIXEL |
         XCB_CW_BORDER_PIXEL |
         XCB_CW_EVENT_MASK |
         XCB_CW_COLORMAP),
        (const uint32_t []){
            screen->black_pixel,
            screen->white_pixel,
            (XCB_EVENT_MASK_EXPOSURE |
             XCB_EVENT_MASK_BUTTON_RELEASE |
             XCB_EVENT_MASK_BUTTON_PRESS |
             XCB_EVENT_MASK_POINTER_MOTION |
             XCB_EVENT_MASK_STRUCTURE_NOTIFY),
            bar->colormap}
        );

    const char *title = "hello world";
    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        strlen(title), title);

    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, (const uint32_t []){getpid()});
    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_WINDOW_TYPE, XCB_ATOM_ATOM, 32,
        1, (const uint32_t []){_NET_WM_WINDOW_TYPE_DOCK});
    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_STATE, XCB_ATOM_ATOM, 32,
        2, (const uint32_t []){_NET_WM_STATE_ABOVE, _NET_WM_STATE_STICKY});
    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, (const uint32_t []){0xffffffff});

    /* Always on top */
    xcb_configure_window(
        bar->conn, bar->win, XCB_CONFIG_WINDOW_STACK_MODE,
        (const uint32_t []){XCB_STACK_MODE_ABOVE});

    uint32_t top_strut, bottom_strut;
    uint32_t top_pair[2], bottom_pair[2];

    if (bar->location == BAR_TOP) {
        top_strut = bar->y + bar->height_with_border;
        top_pair[0] = bar->x;
        top_pair[1] = bar->x + bar->width - 1;

        bottom_strut = 0;
        bottom_pair[0] = bottom_pair[1] = 0;
    } else {
        bottom_strut = screen->height_in_pixels - bar->y;
        bottom_pair[0] = bar->x;
        bottom_pair[1] = bar->x + bar->width - 1;

        top_strut = 0;
        top_pair[0] = top_pair[1] = 0;
    }

    uint32_t strut[] = {
        /* left/right/top/bottom */
        0, 0,
        top_strut,
        bottom_strut,

        /* start/end pairs for left/right/top/bottom */
        0, 0,
        0, 0,
        top_pair[0], top_pair[1],
        bottom_pair[0], bottom_pair[1],
    };

    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_STRUT, XCB_ATOM_CARDINAL, 32,
        4, strut);

    xcb_change_property(
        bar->conn,
        XCB_PROP_MODE_REPLACE, bar->win,
        _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 32,
        12, strut);

    bar->pixmap = xcb_generate_id(bar->conn);
    xcb_create_pixmap(bar->conn, depth, bar->pixmap, bar->win,
                      bar->width, bar->height_with_border);

    bar->gc = xcb_generate_id(bar->conn);
    xcb_create_gc(bar->conn, bar->gc, bar->pixmap,
                  XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
                  (const uint32_t []){screen->white_pixel, 0});

    cairo_surface_t *surface = cairo_xcb_surface_create(
        bar->conn, bar->pixmap, vis, bar->width, bar->height_with_border);
    bar->cairo = cairo_create(surface);

    xcb_map_window(bar->conn, bar->win);
    xcb_flush(bar->conn);

    /* Start modules */
    thrd_t thrd_left[bar->left.count];
    thrd_t thrd_center[bar->center.count];
    thrd_t thrd_right[bar->right.count];

    struct module_run_context run_ctx_left[bar->left.count];
    struct module_run_context run_ctx_center[bar->center.count];
    struct module_run_context run_ctx_right[bar->right.count];

    int ready_fd = eventfd(0, EFD_SEMAPHORE);
    assert(ready_fd != -1);

    for (size_t i = 0; i < bar->left.count; i++) {
        struct module_run_context *ctx = &run_ctx_left[i];

        ctx->module = bar->left.mods[i];
        ctx->ready_fd = ready_fd;
        ctx->abort_fd = run_ctx->abort_fd;

        thrd_create(&thrd_left[i], (int (*)(void *))bar->left.mods[i]->run, ctx);
    }
    for (size_t i = 0; i < bar->center.count; i++) {
        struct module_run_context *ctx = &run_ctx_center[i];

        ctx->module = bar->center.mods[i];
        ctx->ready_fd = ready_fd;
        ctx->abort_fd = run_ctx->abort_fd;

        thrd_create(&thrd_center[i], (int (*)(void *))bar->center.mods[i]->run, ctx);
    }
    for (size_t i = 0; i < bar->right.count; i++) {
        struct module_run_context *ctx = &run_ctx_right[i];

        ctx->module = bar->right.mods[i];
        ctx->ready_fd = ready_fd;
        ctx->abort_fd = run_ctx->abort_fd;

        thrd_create(&thrd_right[i], (int (*)(void *))bar->right.mods[i]->run, ctx);
    }

    LOG_DBG("waiting for modules to become ready");

    for (size_t i = 0; i < (bar->left.count +
                            bar->center.count +
                            bar->right.count); i++) {
        uint64_t b;
        read(ready_fd, &b, sizeof(b));
    }

    close(ready_fd);
    LOG_DBG("all modules started");

    refresh(_bar);

    int fd = xcb_get_file_descriptor(bar->conn);

    while (true) {
        struct pollfd fds[] = {
            {.fd = run_ctx->abort_fd, .events = POLLIN},
            {.fd = fd, .events = POLLIN}
        };

        int ret = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        assert(ret == 1);

        if (fds[0].revents && POLLIN)
            break;

        for (xcb_generic_event_t *e = xcb_wait_for_event(bar->conn);
             e != NULL;
             e = xcb_poll_for_event(bar->conn))
        {
            switch (XCB_EVENT_RESPONSE_TYPE(e)) {
            case XCB_EXPOSE:
                expose(_bar);
                break;

            case XCB_BUTTON_RELEASE:
            case XCB_BUTTON_PRESS:
            case XCB_MOTION_NOTIFY:
            case XCB_DESTROY_NOTIFY:
            case XCB_REPARENT_NOTIFY:
            case XCB_CONFIGURE_NOTIFY:
            case XCB_MAP_NOTIFY:
                LOG_WARN("unimplemented event: %d", XCB_EVENT_RESPONSE_TYPE(e));
                break;

            default:
                LOG_ERR("unsupported event: %d", XCB_EVENT_RESPONSE_TYPE(e));
                break;
            }

            free(e);
            xcb_flush(bar->conn);
        }
    }

    /* Wait for modules to terminate */
    int mod_ret;
    for (size_t i = 0; i < bar->left.count; i++)
        thrd_join(thrd_left[i], &mod_ret);
    for (size_t i = 0; i < bar->center.count; i++)
        thrd_join(thrd_center[i], &mod_ret);
    for (size_t i = 0; i < bar->right.count; i++)
        thrd_join(thrd_right[i], &mod_ret);

    LOG_DBG("modules joined");

    cairo_destroy(bar->cairo);
    cairo_surface_destroy(surface);
    cairo_debug_reset_static_data();

    xcb_free_gc(bar->conn, bar->gc);
    xcb_free_pixmap(bar->conn, bar->pixmap);
    xcb_destroy_window(bar->conn, bar->win);
    xcb_free_colormap(bar->conn, bar->colormap);
    xcb_flush(bar->conn);

    xcb_disconnect(bar->conn);

    LOG_DBG("bar exiting");
    return 0;
}

static void
refresh(const struct bar *bar)
{
    const struct private *b = bar->private;

    /* Send an event to handle refresh from main thread */

    /* Note: docs say that all X11 events are 32 bytes, reglardless of
     * the size of the event structure */
    xcb_expose_event_t *evt = calloc(32, 1);

    *evt = (xcb_expose_event_t){
        .response_type = XCB_EXPOSE,
        .window = b->win,
        .x = 0,
        .y = 0,
        .width = b->width,
        .height = b->height,
        .count = 1
    };

    xcb_send_event(b->conn, false, b->win, XCB_EVENT_MASK_EXPOSURE, (char *)evt);
    xcb_flush(b->conn);
    free(evt);
}

static void
destroy(struct bar *bar)
{
    struct private *b = bar->private;

    for (size_t i = 0; i < b->left.count; i++)
        b->left.mods[i]->destroy(b->left.mods[i]);
    for (size_t i = 0; i < b->center.count; i++)
        b->center.mods[i]->destroy(b->center.mods[i]);
    for (size_t i = 0; i < b->right.count; i++)
        b->right.mods[i]->destroy(b->right.mods[i]);

    free(b->left.mods);
    free(b->center.mods);
    free(b->right.mods);

    free(bar->private);
    free(bar);
}

struct bar *
bar_new(const struct bar_config *config)
{
    struct private *priv = malloc(sizeof(*priv));
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
    priv->center.mods = malloc(config->center.count * sizeof(priv->center.mods[0]));
    priv->right.mods = malloc(config->right.count * sizeof(priv->right.mods[0]));
    priv->left.count = config->left.count;
    priv->center.count = config->center.count;
    priv->right.count = config->right.count;

    for (size_t i = 0; i < priv->left.count; i++)
        priv->left.mods[i] = config->left.mods[i];
    for (size_t i = 0; i < priv->center.count; i++)
        priv->center.mods[i] = config->center.mods[i];
    for (size_t i = 0; i < priv->right.count; i++)
        priv->right.mods[i] = config->right.mods[i];

    struct bar *bar = malloc(sizeof(*bar));
    bar->private = priv;
    bar->run = &run;
    bar->destroy = &destroy;
    bar->refresh = &refresh;

    for (size_t i = 0; i < priv->left.count; i++)
        priv->left.mods[i]->bar = bar;
    for (size_t i = 0; i < priv->center.count; i++)
        priv->center.mods[i]->bar = bar;
    for (size_t i = 0; i < priv->right.count; i++)
        priv->right.mods[i]->bar = bar;

    return bar;
}
