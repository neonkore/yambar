#include "xcb.h"

#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include <pixman.h>
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
#include "../stride.h"
#include "../xcb.h"

struct xcb_backend {
    int x, y;

    xcb_connection_t *conn;

    xcb_window_t win;
    xcb_colormap_t colormap;
    xcb_gc_t gc;
    xcb_cursor_context_t *cursor_ctx;
    xcb_cursor_t cursor;
    const char *xcursor;

    uint8_t depth;
    void *client_pixmap;
    size_t client_pixmap_size;
    pixman_image_t *pix;

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

    if (bar->border.left_margin != 0 ||
        bar->border.right_margin != 0 ||
        bar->border.top_margin != 0 ||
        bar->border.bottom_margin)
    {
        LOG_WARN("non-zero border margins ignored in X11 backend");
    }

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

        /* User wants a specific monitor, and this is not the one */
        if (bar->monitor != NULL && strcmp(bar->monitor, name) != 0) {
            free(name);
            continue;
        }

        backend->x = mon->x;
        backend->y = mon->y;
        bar->width = mon->width;
        backend->y += bar->location == BAR_TOP ? 0
            : screen->height_in_pixels - bar->height_with_border;

        found_monitor = true;

        if ((bar->monitor != NULL && strcmp(bar->monitor, name) == 0) ||
            (bar->monitor == NULL && mon->primary))
        {
            /* Exact match */
            free(name);
            break;
        }

        free(name);
    }
    free(monitors);

    if (!found_monitor) {
        if (bar->monitor == NULL)
            LOG_ERR("no monitors found");
        else
            LOG_ERR("no monitor '%s'", bar->monitor);

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
    backend->depth = depth;
    LOG_DBG("using a %hhu-bit visual", depth);

    backend->colormap = xcb_generate_id(backend->conn);
    xcb_create_colormap(
        backend->conn, 0, backend->colormap, screen->root, vis->visual_id);

    backend->win = xcb_generate_id(backend->conn);
    xcb_create_window(
        backend->conn,
        depth, backend->win, screen->root,
        backend->x, backend->y, bar->width, bar->height_with_border,
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

    const char *title = "yambar";
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
        top_strut = backend->y + bar->height_with_border;
        top_pair[0] = backend->x;
        top_pair[1] = backend->x + bar->width - 1;

        bottom_strut = 0;
        bottom_pair[0] = bottom_pair[1] = 0;
    } else {
        bottom_strut = screen->height_in_pixels - backend->y;
        bottom_pair[0] = backend->x;
        bottom_pair[1] = backend->x + bar->width - 1;

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

    backend->gc = xcb_generate_id(backend->conn);
    xcb_create_gc(backend->conn, backend->gc, backend->win,
                  XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
                  (const uint32_t []){screen->white_pixel, 0});

    const uint32_t stride = stride_for_format_and_width(
        PIXMAN_a8r8g8b8, bar->width);

    backend->client_pixmap_size = stride * bar->height_with_border;
    backend->client_pixmap = malloc(backend->client_pixmap_size);
    backend->pix = pixman_image_create_bits_no_clear(
        PIXMAN_a8r8g8b8, bar->width, bar->height_with_border,
        (uint32_t *)backend->client_pixmap, stride);
    bar->pix = backend->pix;

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

    if (backend->pix != NULL)
        pixman_image_unref(backend->pix);
    free(backend->client_pixmap);

    if (backend->gc != 0)
        xcb_free_gc(backend->conn, backend->gc);
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

    pthread_setname_np(pthread_self(), "bar(xcb)");

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
            if (write(_bar->abort_fd, &(uint64_t){1}, sizeof(uint64_t))
                != sizeof(uint64_t))
            {
                LOG_ERRNO("failed to signal abort to modules");
            }
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
commit(const struct bar *_bar)
{
    const struct private *bar = _bar->private;
    const struct xcb_backend *backend = bar->backend.data;

    xcb_put_image(
        backend->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, backend->win, backend->gc,
        bar->width, bar->height_with_border, 0, 0, 0,
        backend->depth, backend->client_pixmap_size, backend->client_pixmap);
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

    if (backend->xcursor != NULL && strcmp(backend->xcursor, cursor) == 0)
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
    .commit = &commit,
    .refresh = &refresh,
    .set_cursor = &set_cursor,
};
