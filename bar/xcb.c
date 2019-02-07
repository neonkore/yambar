#include "xcb.h"

#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <poll.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/render.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_ewmh.h>

#include "private.h"

#define LOG_MODULE "bar:xcb"
#include "../log.h"
#include "../xcb.h"

struct xcb_backend {
    xcb_connection_t *conn;

    xcb_window_t win;
    xcb_colormap_t colormap;
    xcb_pixmap_t pixmap;
    xcb_gc_t gc;
    xcb_cursor_context_t *cursor_ctx;
    xcb_cursor_t cursor;
};

void *
bar_backend_xcb_new(void)
{
    xcb_init();
    return calloc(1, sizeof(struct xcb_backend));
}

static bool
setup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct xcb_backend *backend = bar->backend.data;

    /* TODO: a lot of this (up to mapping the window) could be done in bar_new() */
    xcb_generic_error_t *e;

    int default_screen;
    backend->conn = xcb_connect(NULL, &default_screen);
    if (xcb_connection_has_error(backend->conn) > 0) {
        LOG_ERR("failed to connect to X");
        xcb_disconnect(backend->conn);
        return false;
    }

    xcb_screen_t *screen = xcb_aux_get_screen(backend->conn, default_screen);

    xcb_randr_get_monitors_reply_t *monitors = xcb_randr_get_monitors_reply(
        backend->conn,
        xcb_randr_get_monitors(backend->conn, screen->root, 0),
        &e);

    if (e != NULL) {
        LOG_ERR("failed to get monitor list: %s", xcb_error(e));
        free(e);
        /* TODO: cleanup (disconnect) */
        return false;
    }

    /* Find monitor coordinates and width/height */
    bool found_monitor = false;
    for (xcb_randr_monitor_info_iterator_t it =
             xcb_randr_get_monitors_monitors_iterator(monitors);
         it.rem > 0;
         xcb_randr_monitor_info_next(&it))
    {
        const xcb_randr_monitor_info_t *mon = it.data;
        char *name = get_atom_name(backend->conn, mon->name);

        LOG_INFO("monitor: %s: %ux%u+%u+%u (%ux%umm)", name,
               mon->width, mon->height, mon->x, mon->y,
               mon->width_in_millimeters, mon->height_in_millimeters);

        if (!((bar->monitor == NULL && mon->primary) ||
              (bar->monitor != NULL && strcmp(bar->monitor, name) == 0)))
        {
            free(name);
            continue;
        }

        free(name);

        bar->x = mon->x;
        bar->y = mon->y;
        bar->width = mon->width;
        bar->y += bar->location == BAR_TOP ? 0
            : screen->height_in_pixels - bar->height_with_border;
        found_monitor = true;
        break;
    }
    free(monitors);

    if (!found_monitor) {
        LOG_ERR("no matching monitor");
        /* TODO: cleanup */
        return false;
    }

    uint8_t depth = 0;
    xcb_visualtype_t *vis = xcb_aux_find_visual_by_attrs(screen, -1, 32);

    if (vis != NULL)
        depth = 32;
    else {
        vis = xcb_aux_find_visual_by_attrs(screen, -1, 24);
        if (vis != NULL)
            depth = 24;
    }

    assert(depth == 32 || depth == 24);
    assert(vis != NULL);
    LOG_DBG("using a %hhu-bit visual", depth);

    backend->colormap = xcb_generate_id(backend->conn);
    xcb_create_colormap(
        backend->conn, 0, backend->colormap, screen->root, vis->visual_id);

    backend->win = xcb_generate_id(backend->conn);
    xcb_create_window(
        backend->conn,
        depth, backend->win, screen->root,
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
            backend->colormap}
        );

    const char *title = "f00bar";
    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        strlen(title), title);

    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, (const uint32_t []){getpid()});
    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_WINDOW_TYPE, XCB_ATOM_ATOM, 32,
        1, (const uint32_t []){_NET_WM_WINDOW_TYPE_DOCK});
    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_STATE, XCB_ATOM_ATOM, 32,
        2, (const uint32_t []){_NET_WM_STATE_ABOVE, _NET_WM_STATE_STICKY});
    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, (const uint32_t []){0xffffffff});

    /* Always on top */
    xcb_configure_window(
        backend->conn, backend->win, XCB_CONFIG_WINDOW_STACK_MODE,
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
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_STRUT, XCB_ATOM_CARDINAL, 32,
        4, strut);

    xcb_change_property(
        backend->conn,
        XCB_PROP_MODE_REPLACE, backend->win,
        _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 32,
        12, strut);

    backend->pixmap = xcb_generate_id(backend->conn);
    xcb_create_pixmap(backend->conn, depth, backend->pixmap, backend->win,
                      bar->width, bar->height_with_border);

    backend->gc = xcb_generate_id(backend->conn);
    xcb_create_gc(backend->conn, backend->gc, backend->pixmap,
                  XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
                  (const uint32_t []){screen->white_pixel, 0});

    LOG_DBG("cairo: %s", cairo_version_string());
    bar->cairo_surface = cairo_xcb_surface_create(
        backend->conn, backend->pixmap, vis, bar->width, bar->height_with_border);
    bar->cairo = cairo_create(bar->cairo_surface);

    xcb_map_window(backend->conn, backend->win);

    if (xcb_cursor_context_new(backend->conn, screen, &backend->cursor_ctx) < 0)
        LOG_WARN("failed to create XCB cursor context");

    xcb_flush(backend->conn);
    return true;
}

static void
cleanup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct xcb_backend *backend = bar->backend.data;

    if (backend->conn == NULL)
        return;

    if (backend->cursor != 0)
        xcb_free_cursor(backend->conn, backend->cursor);
    if (backend->cursor_ctx != NULL)
        xcb_cursor_context_free(backend->cursor_ctx);

    /* TODO: move to bar.c */
    free(bar->cursor_name);

    if (backend->gc != 0)
        xcb_free_gc(backend->conn, backend->gc);
    if (backend->pixmap != 0)
        xcb_free_pixmap(backend->conn, backend->pixmap);
    if (backend->win != 0)
        xcb_destroy_window(backend->conn, backend->win);
    if (backend->colormap != 0)
        xcb_free_colormap(backend->conn, backend->colormap);

    xcb_flush(backend->conn);
    xcb_disconnect(backend->conn);
    backend->conn = NULL;
}

static void
loop(struct bar *_bar,
     void (*expose)(const struct bar *bar),
     void (*on_mouse)(struct bar *bar, enum mouse_event event, int x, int y))
{
    struct private *bar = _bar->private;
    struct xcb_backend *backend = bar->backend.data;

    const int fd = xcb_get_file_descriptor(backend->conn);

    while (true) {
        struct pollfd fds[] = {
            {.fd = _bar->abort_fd, .events = POLLIN},
            {.fd = fd, .events = POLLIN}
        };

        poll(fds, sizeof(fds) / sizeof(fds[0]), -1);

        if (fds[0].revents && POLLIN)
            break;

        if (fds[1].revents & POLLHUP) {
            LOG_WARN("disconnected from XCB");
            write(_bar->abort_fd, &(uint64_t){1}, sizeof(uint64_t));
            break;
        }

        for (xcb_generic_event_t *e = xcb_wait_for_event(backend->conn);
             e != NULL;
             e = xcb_poll_for_event(backend->conn))
        {
            switch (XCB_EVENT_RESPONSE_TYPE(e)) {
            case 0:
                LOG_ERR("XCB: %s", xcb_error((const xcb_generic_error_t *)e));
                break;

            case XCB_EXPOSE:
                expose(_bar);
                break;

            case XCB_MOTION_NOTIFY: {
                const xcb_motion_notify_event_t *evt = (void *)e;
                on_mouse(_bar, ON_MOUSE_MOTION, evt->event_x, evt->event_y);
                break;
            }

            case XCB_BUTTON_PRESS:
                break;

            case XCB_BUTTON_RELEASE: {
                const xcb_button_release_event_t *evt = (void *)e;
                on_mouse(_bar, ON_MOUSE_CLICK, evt->event_x, evt->event_y);
                break;
            }

            case XCB_DESTROY_NOTIFY:
                LOG_WARN("unimplemented event: XCB_DESTROY_NOTIFY");
                break;

            case XCB_REPARENT_NOTIFY:
            case XCB_CONFIGURE_NOTIFY:
            case XCB_MAP_NOTIFY:
            case XCB_MAPPING_NOTIFY:
                /* Just ignore */
                break;

            default:
                LOG_ERR("unsupported event: %d", XCB_EVENT_RESPONSE_TYPE(e));
                break;
            }

            free(e);
            xcb_flush(backend->conn);
        }
    }
}

static void
commit_surface(const struct bar *_bar)
{
    const struct private *bar = _bar->private;
    const struct xcb_backend *backend = bar->backend.data;
    xcb_copy_area(backend->conn, backend->pixmap, backend->win, backend->gc,
                  0, 0, 0, 0, bar->width, bar->height_with_border);
    xcb_flush(backend->conn);
}

static void
refresh(const struct bar *_bar)
{
    const struct private *bar = _bar->private;
    const struct xcb_backend *backend = bar->backend.data;

    /* Send an event to handle refresh from main thread */

    /* Note: docs say that all X11 events are 32 bytes, reglardless of
     * the size of the event structure */
    xcb_expose_event_t *evt = calloc(32, 1);

    *evt = (xcb_expose_event_t){
        .response_type = XCB_EXPOSE,
        .window = backend->win,
        .x = 0,
        .y = 0,
        .width = bar->width,
        .height = bar->height,
        .count = 1
    };

    xcb_send_event(
        backend->conn, false, backend->win, XCB_EVENT_MASK_EXPOSURE,
        (char *)evt);

    xcb_flush(backend->conn);
    free(evt);
}

static void
set_cursor(struct bar *_bar, const char *cursor)
{
    struct private *bar = _bar->private;
    struct xcb_backend *backend = bar->backend.data;

    if (backend->cursor_ctx == NULL)
        return;

    if (backend->cursor != 0)
        xcb_free_cursor(backend->conn, backend->cursor);

    backend->cursor = xcb_cursor_load_cursor(backend->cursor_ctx, cursor);
    xcb_change_window_attributes(
        backend->conn, backend->win, XCB_CW_CURSOR, &backend->cursor);
}

const struct backend xcb_backend_iface = {
    .setup = &setup,
    .cleanup = &cleanup,
    .loop = &loop,
    .commit_surface = &commit_surface,
    .refresh = &refresh,
    .set_cursor = &set_cursor,
};
