#include "wayland.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

#include <sys/mman.h>
#include <linux/memfd.h>

#include <cairo.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#define LOG_MODULE "bar:wayland"
#include "../log.h"
#include "../tllist.h"

#include "private.h"

struct buffer {
    bool busy;
    size_t size;
    void *mmapped;

    struct wl_buffer *wl_buf;

    cairo_surface_t *cairo_surface;
    cairo_t *cairo;
};

struct monitor {
    struct wayland_backend *backend;

    struct wl_output *output;
    struct zxdg_output_v1 *xdg;
    char *name;

    int x;
    int y;

    int width_mm;
    int height_mm;

    int width_px;
    int height_px;

    int scale;
};

struct wayland_backend {
    struct bar *bar;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_shm *shm;
    struct wl_seat *seat;

    struct {
        struct wl_pointer *pointer;
        uint32_t serial;

        int x;
        int y;

        struct wl_surface *surface;
        struct wl_cursor_theme *theme;
        struct wl_cursor *cursor;
    } pointer;

    tll(struct monitor) monitors;
    const struct monitor *monitor;

    struct zxdg_output_manager_v1 *xdg_output_manager;

    /* TODO: set directly in bar instead */
    int width, height;

    /* Used to signal e.g. refresh */
    int pipe_fds[2];

    /* We're already waiting for a frame done callback */
    bool render_scheduled;

    tll(struct buffer) buffers;     /* List of SHM buffers */
    struct buffer *next_buffer;     /* Bar is rendering to this one */
    struct buffer *pending_buffer;  /* Finished, but not yet rendered */

    void (*bar_expose)(const struct bar *bar);
    void (*bar_on_mouse)(struct bar *bar, enum mouse_event event, int x, int y);
};

void *
bar_backend_wayland_new(void)
{
    return calloc(1, sizeof(struct wayland_backend));
}

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    //printf("SHM format: 0x%08x\n", format);
}

static const struct wl_shm_listener shm_listener = {
    .format = &shm_format,
};

static void
update_cursor_surface(struct wayland_backend *backend)
{
    if (backend->pointer.cursor == NULL)
        return;

    struct wl_cursor_image *image = backend->pointer.cursor->images[0];

    wl_surface_attach(
        backend->pointer.surface, wl_cursor_image_get_buffer(image), 0, 0);

    wl_pointer_set_cursor(
        backend->pointer.pointer, backend->pointer.serial,
        backend->pointer.surface, image->hotspot_x, image->hotspot_y);

    wl_surface_damage_buffer(
        backend->pointer.surface, 0, 0, INT32_MAX, INT32_MAX);

    wl_surface_commit(backend->pointer.surface);
}

static void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface,
                 wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct wayland_backend *backend = data;
    backend->pointer.serial = serial;
    backend->pointer.x = wl_fixed_to_int(surface_x);
    backend->pointer.y = wl_fixed_to_int(surface_y);

    update_cursor_surface(backend);
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface)
{
}

static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                  uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct wayland_backend *backend = data;

    backend->pointer.x = wl_fixed_to_int(surface_x);
    backend->pointer.y = wl_fixed_to_int(surface_y);

    backend->bar_on_mouse(
        backend->bar, ON_MOUSE_MOTION, backend->pointer.x, backend->pointer.y);
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                  uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;

    struct wayland_backend *backend = data;
    backend->bar_on_mouse(
        backend->bar, ON_MOUSE_CLICK, backend->pointer.x, backend->pointer.y);
}

static void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
}

static void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                       uint32_t axis_source)
{
}

static void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                     uint32_t time, uint32_t axis)
{
}

static void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                         uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = wl_pointer_frame,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
    struct wayland_backend *backend = data;

    if (backend->pointer.pointer != NULL) {
        wl_pointer_release(backend->pointer.pointer);
        backend->pointer.pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
        backend->pointer.pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(backend->pointer.pointer, &pointer_listener, backend);
    }
}

static void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void
output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                int32_t physical_width, int32_t physical_height,
                int32_t subpixel, const char *make, const char *model,
                int32_t transform)
{
    struct monitor *mon = data;
    mon->width_mm = physical_width;
    mon->height_mm = physical_height;
}

static void
output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
            int32_t width, int32_t height, int32_t refresh)
{
}

static void
output_done(void *data, struct wl_output *wl_output)
{
}

static void
output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
    struct monitor *mon = data;
    mon->scale = factor;
}


static const struct wl_output_listener output_listener = {
    .geometry = &output_geometry,
    .mode = &output_mode,
    .done = &output_done,
    .scale = &output_scale,
};

static void
xdg_output_handle_logical_position(void *data,
                                   struct zxdg_output_v1 *xdg_output,
                                   int32_t x, int32_t y)
{
    struct monitor *mon = data;
    mon->x = x;
    mon->y = y;
}

static void
xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                               int32_t width, int32_t height)
{
    struct monitor *mon = data;
    mon->width_px = width;
    mon->height_px = height;
}

static void
xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
                       const char *name)
{
    struct monitor *mon = data;
    mon->name = strdup(name);
}

static void
xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
                              const char *description)
{
}

static struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct wayland_backend *backend = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        backend->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, 4);
    }

    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        backend->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(backend->shm, &shm_listener, backend);
        wl_display_roundtrip(backend->display);
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(
            registry, name, &wl_output_interface, 3);

        tll_push_back(backend->monitors, ((struct monitor){
                    .backend  = backend,
                    .output = output}));

        struct monitor *mon = &tll_back(backend->monitors);
        wl_output_add_listener(output, &output_listener, mon);

        /*
         * The "output" interface doesn't give us the monitors'
         * identifiers (e.g. "LVDS-1"). Use the XDG output interface
         * for that.
         */

        assert(backend->xdg_output_manager != NULL);
        mon->xdg = zxdg_output_manager_v1_get_xdg_output(
            backend->xdg_output_manager, mon->output);

        zxdg_output_v1_add_listener(mon->xdg, &xdg_output_listener, mon);
        wl_display_roundtrip(backend->display);
    }

    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        backend->layer_shell = wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, 1);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        backend->seat = wl_registry_bind(registry, name, &wl_seat_interface, 3);
        wl_seat_add_listener(backend->seat, &seat_listener, backend);
        wl_display_roundtrip(backend->display);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        backend->xdg_output_manager = wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, 2);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    LOG_WARN("global removed: %u", name);
}

static const struct wl_registry_listener registry_listener = {
    .global = &handle_global,
    .global_remove = &handle_global_remove,
};

static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t w, uint32_t h)
{
    struct wayland_backend *backend = data;
    backend->width = w;
    backend->height = h;

    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
};

static void
buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    //printf("buffer release\n");
    struct buffer *buffer = data;
    assert(buffer->busy);
    buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = &buffer_release,
};

static struct buffer *
get_buffer(struct wayland_backend *backend)
{
    tll_foreach(backend->buffers, it) {
        if (!it->item.busy) {
            it->item.busy = true;
            return &it->item;
        }
    }

    /*
     * No existing buffer available. Create a new one by:
     *
     * 1. open a memory backed "file" with memfd_create()
     * 2. mmap() the memory file, to be used by the cairo surface
     * 3. create a wayland shm buffer for the same memory file
     *
     * The cairo surface and the wayland buffer are now sharing
     * memory.
     */

    int pool_fd = -1;
    void *mmapped = NULL;
    size_t size = 0;

    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buf = NULL;

    cairo_surface_t *cairo_surface = NULL;
    cairo_t *cairo = NULL;

    /* Backing memory for SHM */
    pool_fd = memfd_create("f00bar-wayland-shm-buffer-pool", MFD_CLOEXEC);
    if (pool_fd == -1) {
        LOG_ERRNO("failed to create SHM backing memory file");
        goto err;
    }

    /* Total size */
    const uint32_t stride = cairo_format_stride_for_width(
        CAIRO_FORMAT_ARGB32, backend->width);
    size = stride * backend->height;
    ftruncate(pool_fd, size);

    mmapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool_fd, 0);
    if (mmapped == MAP_FAILED) {
        LOG_ERR("failed to mmap SHM backing memory file");
        goto err;
    }

    pool = wl_shm_create_pool(backend->shm, pool_fd, size);
    if (pool == NULL) {
        LOG_ERR("failed to create SHM pool");
        goto err;
    }

    buf = wl_shm_pool_create_buffer(
        pool, 0, backend->width, backend->height, stride, WL_SHM_FORMAT_ARGB8888);
    if (buf == NULL) {
        LOG_ERR("failed to create SHM buffer");
        goto err;
    }

    /* We use the entire pool for our single buffer */
    wl_shm_pool_destroy(pool); pool = NULL;
    close(pool_fd); pool_fd = -1;

    /* Create a cairo surface around the mmapped memory */
    cairo_surface = cairo_image_surface_create_for_data(
        mmapped, CAIRO_FORMAT_ARGB32, backend->width, backend->height, stride);
    if (cairo_surface_status(cairo_surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("failed to create cairo surface: %s",
                cairo_status_to_string(cairo_surface_status(cairo_surface)));
        goto err;
    }

    cairo = cairo_create(cairo_surface);
    if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("failed to create cairo context: %s",
                cairo_status_to_string(cairo_status(cairo)));
        goto err;
    }

    /* Push to list of available buffers, but marked as 'busy' */
    tll_push_back(
        backend->buffers,
        ((struct buffer){
            .busy = true,
            .size = size,
            .mmapped = mmapped,
            .wl_buf = buf,
            .cairo_surface = cairo_surface,
            .cairo = cairo}
            )
        );

    struct buffer *ret = &tll_back(backend->buffers);
    wl_buffer_add_listener(ret->wl_buf, &buffer_listener, ret);
    return ret;

err:
    if (cairo != NULL)
        cairo_destroy(cairo);
    if (cairo_surface != NULL)
        cairo_surface_destroy(cairo_surface);
    if (buf != NULL)
        wl_buffer_destroy(buf);
    if (pool != NULL)
        wl_shm_pool_destroy(pool);
    if (pool_fd != -1)
        close(pool_fd);
    if (mmapped != NULL)
        munmap(mmapped, size);

    return NULL;
}

static bool
setup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    backend->bar = _bar;

    backend->display = wl_display_connect(NULL);
    if (backend->display == NULL) {
        LOG_ERR("failed to connect to wayland; no compistor running?");
        return false;
    }

    backend->registry = wl_display_get_registry(backend->display);
    if (backend->registry == NULL) {
        LOG_ERR("failed to get wayland registry");
        return false;
    }

    wl_registry_add_listener(backend->registry, &registry_listener, backend);
    wl_display_roundtrip(backend->display);

    if (backend->compositor == NULL) {
        LOG_ERR("no compositor");
        return false;
    }
    if (backend->layer_shell == NULL) {
        LOG_ERR("no layer shell interface");
        return false;
    }
    if (backend->shm == NULL) {
        LOG_ERR("no shared memory buffers interface");
        return false;
    }

    if (tll_length(backend->monitors) == 0) {
        LOG_ERR("no monitors");
        return false;
    }

    tll_foreach(backend->monitors, it) {
        const struct monitor *mon = &it->item;
        LOG_INFO("monitor: %s: %dx%d+%d+%d (%dx%dmm)",
                 mon->name, mon->width_px, mon->height_px,
                 mon->x, mon->y, mon->width_mm, mon->height_mm);

        /* TODO: detect primary output when user hasn't specified a monitor */
        if (bar->monitor == NULL)
            backend->monitor = mon;
        else if (strcmp(bar->monitor, mon->name) == 0)
            backend->monitor = mon;
    }

    backend->surface = wl_compositor_create_surface(backend->compositor);
    if (backend->surface == NULL) {
        LOG_ERR("failed to create panel surface");
        return false;
    }

    backend->pointer.surface = wl_compositor_create_surface(backend->compositor);
    if (backend->pointer.surface == NULL) {
        LOG_ERR("failed to create cursor surface");
        return false;
    }

    backend->pointer.theme = wl_cursor_theme_load(NULL, 24, backend->shm);
    if (backend->pointer.theme == NULL) {
        LOG_ERR("failed to load cursor theme");
        return false;
    }

    backend->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        backend->layer_shell, backend->surface, backend->monitor->output,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "f00bar");
    if (backend->layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        return false;
    }

    /* Aligned to top, maximum width */
    enum zwlr_layer_surface_v1_anchor top_or_bottom = bar->location == BAR_TOP
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    zwlr_layer_surface_v1_set_anchor(
        backend->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        top_or_bottom);

    zwlr_layer_surface_v1_set_size(
        backend->layer_surface, 0, bar->height_with_border);
    zwlr_layer_surface_v1_set_exclusive_zone(
        backend->layer_surface,
        bar->height_with_border + (bar->location == BAR_TOP ?
                                   bar->border.bottom_margin :
                                   bar->border.top_margin));

    zwlr_layer_surface_v1_set_margin(
        backend->layer_surface,
        bar->border.top_margin,
        bar->border.right_margin,
        bar->border.bottom_margin,
        bar->border.left_margin
        );

    zwlr_layer_surface_v1_add_listener(
        backend->layer_surface, &layer_surface_listener, backend);

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(backend->surface);
    wl_display_roundtrip(backend->display);

    if (backend->width == -1 || backend->height != bar->height_with_border) {
        LOG_ERR("failed to get panel width");
        return false;
    }

    assert(backend->width <= backend->monitor->width_px);
    bar->width = backend->width;

    if (pipe(backend->pipe_fds) == -1) {
        LOG_ERRNO("failed to create pipe");
        return false;
    }

    backend->render_scheduled = false;

    //wl_surface_commit(backend->surface);
    //wl_display_roundtrip(backend->display);

    /* Prepare a buffer + cairo context for bar to draw to */
    backend->next_buffer = get_buffer(backend);
    assert(backend->next_buffer != NULL && backend->next_buffer->busy);

    bar->cairo_surface = backend->next_buffer->cairo_surface;
    bar->cairo = backend->next_buffer->cairo;

    return true;
}

static void
cleanup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    tll_foreach(backend->buffers, it) {
        if (it->item.wl_buf != NULL)
            wl_buffer_destroy(it->item.wl_buf);
        if (it->item.cairo != NULL)
            cairo_destroy(it->item.cairo);
        if (it->item.cairo_surface != NULL)
            cairo_surface_destroy(it->item.cairo_surface);

        munmap(it->item.mmapped, it->item.size);
        tll_remove(backend->buffers, it);
    }

    tll_foreach(backend->monitors, it) {
        struct monitor *mon = &it->item;
        free(mon->name);

        if (mon->xdg != NULL)
            zxdg_output_v1_destroy(mon->xdg);
        if (mon->output != NULL)
            wl_output_destroy(mon->output);
        tll_remove(backend->monitors, it);
    }

    if (backend->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(backend->xdg_output_manager);

    /* TODO: move to bar */
    free(bar->cursor_name);

    if (backend->layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(backend->layer_surface);
    if (backend->layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(backend->layer_shell);
    if (backend->pointer.theme != NULL)
        wl_cursor_theme_destroy(backend->pointer.theme);
    if (backend->pointer.pointer != NULL)
        wl_pointer_destroy(backend->pointer.pointer);
    if (backend->pointer.surface != NULL)
        wl_surface_destroy(backend->pointer.surface);
    if (backend->surface != NULL)
        wl_surface_destroy(backend->surface);
    if (backend->seat != NULL)
        wl_seat_destroy(backend->seat);
    if (backend->compositor != NULL)
        wl_compositor_destroy(backend->compositor);
    if (backend->shm != NULL)
        wl_shm_destroy(backend->shm);
    if (backend->registry != NULL)
        wl_registry_destroy(backend->registry);
    if (backend->display != NULL)
        wl_display_disconnect(backend->display);

    /* Destroyed when freeing buffer list */
    bar->cairo_surface = NULL;
    bar->cairo = NULL;

}

static void
loop(struct bar *_bar,
     void (*expose)(const struct bar *bar),
     void (*on_mouse)(struct bar *bar, enum mouse_event event, int x, int y))
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    backend->bar_expose = expose;
    backend->bar_on_mouse = on_mouse;

#if 0
    while (wl_display_prepare_read(backend->display) != 0){
        //printf("initial wayland event\n");
        wl_display_dispatch_pending(backend->display);
    }
    wl_display_flush(backend->display);
#endif

    wl_display_dispatch_pending(backend->display);
    wl_display_flush(backend->display);

    while (true) {
        struct pollfd fds[] = {
            {.fd = _bar->abort_fd, .events = POLLIN},
            {.fd = wl_display_get_fd(backend->display), .events = POLLIN},
            {.fd = backend->pipe_fds[0], .events = POLLIN},
        };

        wl_display_flush(backend->display);

        //printf("polling\n");
        poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (fds[0].revents & POLLIN) {
            //wl_display_cancel_read(backend->display);
            break;
        }

        if (fds[1].revents & POLLHUP) {
            LOG_WARN("disconnceted from wayland");
            write(_bar->abort_fd, &(uint64_t){1}, sizeof(uint64_t));
            break;
        }

        if (fds[2].revents & POLLIN) {
            uint8_t command;
            //wl_display_cancel_read(backend->display);
            read(backend->pipe_fds[0], &command, sizeof(command));

            assert(command == 1);
            //printf("refresh\n");
            expose(_bar);
#if 0
            while (wl_display_prepare_read(backend->display) != 0) {
                //printf("queued wayland events\n");
                wl_display_dispatch_pending(backend->display);
            }
            wl_display_flush(backend->display);
#endif
        }

        if (fds[1].revents & POLLIN) {
#if 0
            //printf("wayland events\n");
            wl_display_read_events(backend->display);
            wl_display_dispatch_pending(backend->display);
#endif
            //printf("wayland events\n");
            wl_display_dispatch(backend->display);
        }
    }
}

static void frame_callback(
    void *data, struct wl_callback *wl_callback, uint32_t callback_data);

static const struct wl_callback_listener frame_listener = {
    .done = &frame_callback,
};

static void
frame_callback(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
    //printf("frame callback\n");
    struct private *bar = data;
    struct wayland_backend *backend = bar->backend.data;

    backend->render_scheduled = false;

    wl_callback_destroy(wl_callback);

    if (backend->pending_buffer != NULL) {
        struct buffer *buffer = backend->pending_buffer;
        assert(buffer->busy);

        wl_surface_attach(backend->surface, buffer->wl_buf, 0, 0);
        wl_surface_damage(backend->surface, 0, 0, backend->width, backend->height);

        struct wl_callback *cb = wl_surface_frame(backend->surface);
        wl_callback_add_listener(cb, &frame_listener, bar);
        wl_surface_commit(backend->surface);

        backend->pending_buffer = NULL;
        backend->render_scheduled = true;
    } else
        ;//printf("nothing more to do\n");
}

static void
commit_surface(const struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    //printf("commit: %dxl%d\n", backend->width, backend->height);

    assert(backend->next_buffer != NULL);
    assert(backend->next_buffer->busy);

    if (backend->render_scheduled) {
        //printf("already scheduled\n");

        if (backend->pending_buffer != NULL)
            backend->pending_buffer->busy = false;

        backend->pending_buffer = backend->next_buffer;
        backend->next_buffer = NULL;
    } else {

        //printf("scheduling new frame callback\n");
        struct buffer *buffer = backend->next_buffer;
        assert(buffer->busy);

        wl_surface_attach(backend->surface, buffer->wl_buf, 0, 0);
        wl_surface_damage(backend->surface, 0, 0, backend->width, backend->height);

        struct wl_callback *cb = wl_surface_frame(backend->surface);
        wl_callback_add_listener(cb, &frame_listener, bar);
        wl_surface_commit(backend->surface);

        backend->render_scheduled = true;
    }

    backend->next_buffer = get_buffer(backend);
    assert(backend->next_buffer != NULL && backend->next_buffer->busy);

    bar->cairo_surface = backend->next_buffer->cairo_surface;
    bar->cairo = backend->next_buffer->cairo;
}

static void
refresh(const struct bar *_bar)
{
    const struct private *bar = _bar->private;
    const struct wayland_backend *backend = bar->backend.data;

    write(backend->pipe_fds[1], &(uint8_t){1}, sizeof(uint8_t));
}

static void
set_cursor(struct bar *_bar, const char *cursor)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    backend->pointer.cursor = wl_cursor_theme_get_cursor(
        backend->pointer.theme, cursor);

    update_cursor_surface(backend);
}

const struct backend wayland_backend_iface = {
    .setup = &setup,
    .cleanup = &cleanup,
    .loop = &loop,
    .commit_surface = &commit_surface,
    .refresh = &refresh,
    .set_cursor = &set_cursor,
};
