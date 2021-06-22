#include "wayland.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include <sys/mman.h>
#include <linux/memfd.h>
#include <linux/input-event-codes.h>

#include <pixman.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <tllist.h>
#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#define LOG_MODULE "bar:wayland"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../stride.h"

#include "private.h"

struct buffer {
    bool busy;
    size_t width;
    size_t height;
    size_t size;
    void *mmapped;

    struct wl_buffer *wl_buf;

    pixman_image_t *pix;
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

struct seat {
    struct wayland_backend *backend;
    struct wl_seat *seat;
    char *name;
    uint32_t id;

    struct wl_pointer *wl_pointer;
    struct {
        uint32_t serial;

        int x;
        int y;

        struct wl_surface *surface;
        struct wl_cursor_theme *theme;
        struct wl_cursor *cursor;
        const char *xcursor;
        int scale;
    } pointer;
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

    tll(struct seat) seats;
    struct seat *active_seat;

    tll(struct monitor) monitors;
    const struct monitor *monitor;

    int scale;

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
    void (*bar_on_mouse)(struct bar *bar, enum mouse_event event,
                         enum mouse_button btn, int x, int y);
};

static void
seat_destroy(struct seat *seat)
{
    if (seat == NULL)
        return;

    free(seat->name);

    if (seat->pointer.theme != NULL)
        wl_cursor_theme_destroy(seat->pointer.theme);
    if (seat->wl_pointer != NULL)
        wl_pointer_release(seat->wl_pointer);
    if (seat->pointer.surface != NULL)
        wl_surface_destroy(seat->pointer.surface);
    if (seat->seat != NULL)
        wl_seat_release(seat->seat);
}

void *
bar_backend_wayland_new(void)
{
    struct wayland_backend *backend = calloc(1, sizeof(struct wayland_backend));
    backend->pipe_fds[0] = backend->pipe_fds[1] = -1;
    return backend;
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
update_cursor_surface(struct wayland_backend *backend, struct seat *seat)
{
    if (seat->pointer.serial == 0 ||
        seat->pointer.cursor == NULL ||
        seat->pointer.surface == NULL)
    {
        return;
    }

    struct wl_cursor_image *image = seat->pointer.cursor->images[0];

    const int scale = seat->pointer.scale;
    wl_surface_set_buffer_scale(seat->pointer.surface, scale);

    wl_surface_attach(
        seat->pointer.surface, wl_cursor_image_get_buffer(image), 0, 0);

    wl_pointer_set_cursor(
        seat->wl_pointer, seat->pointer.serial,
        seat->pointer.surface,
        image->hotspot_x / scale, image->hotspot_y / scale);


    wl_surface_damage_buffer(
        seat->pointer.surface, 0, 0, INT32_MAX, INT32_MAX);

    wl_surface_commit(seat->pointer.surface);
    wl_display_flush(backend->display);
}

static void
reload_cursor_theme(struct seat *seat, int new_scale)
{
    if (seat->pointer.theme != NULL && seat->pointer.scale == new_scale)
        return;

    if (seat->pointer.theme != NULL) {
        wl_cursor_theme_destroy(seat->pointer.theme);
        seat->pointer.theme = NULL;
        seat->pointer.cursor = NULL;
    }

    unsigned cursor_size = 24;
    const char *cursor_theme = getenv("XCURSOR_THEME");

    {
        const char *env_cursor_size = getenv("XCURSOR_SIZE");
        if (env_cursor_size != NULL) {
            unsigned size;
            if (sscanf(env_cursor_size, "%u", &size) == 1)
                cursor_size = size;
        }
    }

    LOG_INFO("%s: cursor theme: %s, size: %u, scale: %d",
             seat->name, cursor_theme, cursor_size, new_scale);

    struct wl_cursor_theme *theme = wl_cursor_theme_load(
        cursor_theme, cursor_size * new_scale, seat->backend->shm);

    if (theme == NULL) {
        LOG_ERR("%s: failed to load cursor theme", seat->name);
        return;
    }

    seat->pointer.scale = new_scale;
    seat->pointer.theme = theme;
}

static void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface,
                 wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct seat *seat = data;
    struct wayland_backend *backend = seat->backend;

    seat->pointer.serial = serial;
    seat->pointer.x = wl_fixed_to_int(surface_x) * backend->scale;
    seat->pointer.y = wl_fixed_to_int(surface_y) * backend->scale;

    backend->active_seat = seat;
    reload_cursor_theme(seat, backend->monitor->scale);
    update_cursor_surface(backend, seat);
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface)
{
    struct seat *seat = data;
    struct wayland_backend *backend = seat->backend;

    if (backend->active_seat == seat)
        backend->active_seat = NULL;
}

static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                  uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct seat *seat = data;
    struct wayland_backend *backend = seat->backend;

    seat->pointer.x = wl_fixed_to_int(surface_x) * backend->scale;
    seat->pointer.y = wl_fixed_to_int(surface_y) * backend->scale;

    backend->active_seat = seat;
    backend->bar_on_mouse(
        backend->bar, ON_MOUSE_MOTION, MOUSE_BTN_NONE,
        seat->pointer.x, seat->pointer.y);
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                  uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    struct seat *seat = data;
    struct wayland_backend *backend = seat->backend;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        backend->active_seat = seat;
    else {
        enum mouse_button btn;

        switch (button) {
        case BTN_LEFT:   btn = MOUSE_BTN_LEFT; break;
        case BTN_MIDDLE: btn = MOUSE_BTN_MIDDLE; break;
        case BTN_RIGHT:  btn = MOUSE_BTN_RIGHT; break;
        default:
            return;
        }

        backend->bar_on_mouse(
            backend->bar, ON_MOUSE_CLICK, btn, seat->pointer.x, seat->pointer.y);
    }
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
    struct seat *seat = data;

    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        if (seat->wl_pointer == NULL) {
            assert(seat->pointer.surface == NULL);
            seat->pointer.surface = wl_compositor_create_surface(
                seat->backend->compositor);

            if (seat->pointer.surface == NULL) {
                LOG_ERR("%s: failed to create pointer surface", seat->name);
                return;
            }

            seat->wl_pointer = wl_seat_get_pointer(wl_seat);
            wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
        }
    } else {
        if (seat->wl_pointer != NULL) {
            wl_pointer_release(seat->wl_pointer);
            wl_surface_destroy(seat->pointer.surface);

            if (seat->pointer.theme != NULL)
                wl_cursor_theme_destroy(seat->pointer.theme);

            seat->wl_pointer = NULL;
            seat->pointer.surface = NULL;
            seat->pointer.theme = NULL;
            seat->pointer.cursor = NULL;
        }
    }
}

static void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
    struct seat *seat = data;
    free(seat->name);
    seat->name = strdup(name);
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

static bool update_size(struct wayland_backend *backend);
static void refresh(const struct bar *_bar);

static void
output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
    struct monitor *mon = data;
    if (mon->scale == factor)
        return;

    mon->scale = factor;

    if (mon->backend->monitor == mon) {
        int old_scale = mon->backend->scale;
        update_size(mon->backend);

        if (mon->backend->scale != old_scale)
            refresh(mon->backend->bar);
    }
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
    const struct monitor *mon = data;

    LOG_INFO("monitor: %s: %dx%d+%d+%d (%dx%dmm)",
             mon->name, mon->width_px, mon->height_px,
             mon->x, mon->y, mon->width_mm, mon->height_mm);

    struct wayland_backend *backend = mon->backend;
    struct private *bar = backend->bar->private;

    if (bar->monitor != NULL && mon->name != NULL &&
        strcmp(bar->monitor, mon->name) == 0)
    {
        /* User specified a monitor, and this is one */
        backend->monitor = mon;
    }

}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
                       const char *name)
{
    struct monitor *mon = data;
    free(mon->name);
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

static bool
verify_iface_version(const char *iface, uint32_t version, uint32_t wanted)
{
    if (version >= wanted)
        return true;

    LOG_ERR("%s: need interface version %u, but compositor only implements %u",
            iface, wanted, version);
    return false;
}

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    LOG_DBG("global: 0x%08x, interface=%s, version=%u", name, interface, version);
    struct wayland_backend *backend = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        const uint32_t required = 4;
        if (!verify_iface_version(interface, version, required))
            return;

        backend->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, required);
    }

    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        backend->shm = wl_registry_bind(
            registry, name, &wl_shm_interface, required);
        wl_shm_add_listener(backend->shm, &shm_listener, backend);
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t required = 3;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_output *output = wl_registry_bind(
            registry, name, &wl_output_interface, required);

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
        if (backend->xdg_output_manager != NULL) {
            mon->xdg = zxdg_output_manager_v1_get_xdg_output(
                backend->xdg_output_manager, mon->output);

            zxdg_output_v1_add_listener(mon->xdg, &xdg_output_listener, mon);
        }
    }

    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        backend->layer_shell = wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, required);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        const uint32_t required = 5;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_seat *seat = wl_registry_bind(
            registry, name, &wl_seat_interface, required);
        assert(seat != NULL);

        tll_push_back(
            backend->seats, ((struct seat){.backend = backend, .seat = seat, .id = name}));

        wl_seat_add_listener(seat, &seat_listener, &tll_back(backend->seats));
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        backend->xdg_output_manager = wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, required);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct wayland_backend *backend = data;

    tll_foreach(backend->seats, it) {
        if (it->item.id == name) {
            if (backend->active_seat == &it->item)
                backend->active_seat = NULL;

            seat_destroy(&it->item);
            return;
        }
    }

    LOG_WARN("unknown global removed: 0x%08x", name);

    /* TODO: need to handle displays and seats */
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
    backend->width = w * backend->scale;
    backend->height = h * backend->scale;

    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
    struct wayland_backend *backend = data;

    /*
     * Called e.g. when an output is disabled. We don't get a
     * corresponding event if/when that same output re-appears. So,
     * for now, we simply shut down. In the future, we _could_ maybe
     * destroy the surface, listen for output events and re-create the
     * surface if the same output re-appears.
     */
    LOG_WARN("compositor requested surface be closed - shutting down");

    if (write(backend->bar->abort_fd, &(uint64_t){1}, sizeof(uint64_t))
        != sizeof(uint64_t))
    {
        LOG_ERRNO("failed to signal abort to modules");
    }
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = &layer_surface_configure,
    .closed = &layer_surface_closed,
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
        if (!it->item.busy && it->item.width == backend->width && it->item.height == backend->height) {
            it->item.busy = true;
            return &it->item;
        }
    }

    /*
     * No existing buffer available. Create a new one by:
     *
     * 1. open a memory backed "file" with memfd_create()
     * 2. mmap() the memory file, to be used by the pixman image
     * 3. create a wayland shm buffer for the same memory file
     *
     * The pixman image and the wayland buffer are now sharing memory.
     */

    int pool_fd = -1;
    void *mmapped = NULL;
    size_t size = 0;

    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buf = NULL;

    pixman_image_t *pix = NULL;

    /* Backing memory for SHM */
    pool_fd = memfd_create("yambar-wayland-shm-buffer-pool", MFD_CLOEXEC);
    if (pool_fd == -1) {
        LOG_ERRNO("failed to create SHM backing memory file");
        goto err;
    }

    /* Total size */
    const uint32_t stride = stride_for_format_and_width(
        PIXMAN_a8r8g8b8, backend->width);

    size = stride * backend->height;
    if (ftruncate(pool_fd, size) == -1) {
        LOG_ERR("failed to truncate SHM pool");
        goto err;
    }

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

    pix = pixman_image_create_bits_no_clear(
        PIXMAN_a8r8g8b8, backend->width, backend->height, (uint32_t *)mmapped, stride);
    if (pix == NULL) {
        LOG_ERR("failed to create pixman image");
        goto err;
    }

    /* Push to list of available buffers, but marked as 'busy' */
    tll_push_back(
        backend->buffers,
        ((struct buffer){
            .busy = true,
            .width = backend->width,
            .height = backend->height,
            .size = size,
            .mmapped = mmapped,
            .wl_buf = buf,
            .pix = pix,
            })
        );

    struct buffer *ret = &tll_back(backend->buffers);
    wl_buffer_add_listener(ret->wl_buf, &buffer_listener, ret);
    return ret;

err:
    if (pix != NULL)
        pixman_image_unref(pix);
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

static int
guess_scale(const struct wayland_backend *backend)
{
    if (tll_length(backend->monitors) == 0)
        return 1;

    bool all_have_same_scale = true;
    int last_scale = -1;

    tll_foreach(backend->monitors, it) {
        if (last_scale == -1)
            last_scale = it->item.scale;
        else if (last_scale != it->item.scale) {
            all_have_same_scale = false;
            break;
        }
    }

    if (all_have_same_scale) {
        assert(last_scale >= 1);
        return last_scale;
    }

    return 1;
}

static bool
update_size(struct wayland_backend *backend)
{
    struct bar *_bar = backend->bar;
    struct private *bar = _bar->private;

    const struct monitor *mon = backend->monitor;
    const int scale = mon != NULL ? mon->scale : guess_scale(backend);

    if (backend->scale == scale)
        return true;

    backend->scale = scale;

    int height = bar->height_with_border;
    height /= scale;
    height *= scale;
    bar->height = height - 2 * bar->border.width;
    bar->height_with_border = height;

    zwlr_layer_surface_v1_set_size(
        backend->layer_surface, 0, bar->height_with_border / scale);
    zwlr_layer_surface_v1_set_exclusive_zone(
        backend->layer_surface,
        (bar->height_with_border + (bar->location == BAR_TOP
                                    ? bar->border.bottom_margin
                                    : bar->border.top_margin))
        / scale);

    zwlr_layer_surface_v1_set_margin(
        backend->layer_surface,
        bar->border.top_margin / scale,
        bar->border.right_margin / scale,
        bar->border.bottom_margin / scale,
        bar->border.left_margin / scale
        );

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(backend->surface);
    wl_display_roundtrip(backend->display);

    if (backend->width == -1 ||
        backend->height != bar->height_with_border) {
        LOG_ERR("failed to get panel width");
        return false;
    }

    bar->width = backend->width;

    /* Reload buffers */
    if (backend->next_buffer != NULL)
        backend->next_buffer->busy = false;
    backend->next_buffer = get_buffer(backend);
    assert(backend->next_buffer != NULL && backend->next_buffer->busy);
    bar->pix = backend->next_buffer->pix;

    return true;
}

static const struct wl_surface_listener surface_listener;

static bool
setup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    backend->bar = _bar;

    backend->display = wl_display_connect(NULL);
    if (backend->display == NULL) {
        LOG_ERR("failed to connect to wayland; no compositor running?");
        return false;
    }

    backend->registry = wl_display_get_registry(backend->display);
    if (backend->registry == NULL) {
        LOG_ERR("failed to get wayland registry");
        return false;
    }

    /* Globals */
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

    /* Trigger listeners registered in previous roundtrip */
    wl_display_roundtrip(backend->display);

    backend->surface = wl_compositor_create_surface(backend->compositor);
    if (backend->surface == NULL) {
        LOG_ERR("failed to create panel surface");
        return false;
    }

    wl_surface_add_listener(backend->surface, &surface_listener, backend);

    backend->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        backend->layer_shell, backend->surface,
        backend->monitor != NULL ? backend->monitor->output : NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "panel");

    if (backend->layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        return false;
    }

    zwlr_layer_surface_v1_add_listener(
        backend->layer_surface, &layer_surface_listener, backend);

    /* Aligned to top, maximum width */
    enum zwlr_layer_surface_v1_anchor top_or_bottom = bar->location == BAR_TOP
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    zwlr_layer_surface_v1_set_anchor(
        backend->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        top_or_bottom);

    update_size(backend);

    assert(backend->monitor == NULL ||
           backend->width / backend->monitor->scale <= backend->monitor->width_px);

    if (pipe(backend->pipe_fds) == -1) {
        LOG_ERRNO("failed to create pipe");
        return false;
    }

    backend->render_scheduled = false;
    return true;
}

static void
cleanup(struct bar *_bar)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    if (backend->pipe_fds[0] >= 0)
        close(backend->pipe_fds[0]);
    if (backend->pipe_fds[1] >= 0)
        close(backend->pipe_fds[1]);

    tll_foreach(backend->buffers, it) {
        if (it->item.wl_buf != NULL)
            wl_buffer_destroy(it->item.wl_buf);
        if (it->item.pix != NULL)
            pixman_image_unref(it->item.pix);

        munmap(it->item.mmapped, it->item.size);
        tll_remove(backend->buffers, it);
    }

    tll_foreach(backend->monitors, it) {
        struct monitor *mon = &it->item;
        free(mon->name);

        if (mon->xdg != NULL)
            zxdg_output_v1_destroy(mon->xdg);
        if (mon->output != NULL)
            wl_output_release(mon->output);
        tll_remove(backend->monitors, it);
    }

    if (backend->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(backend->xdg_output_manager);

    tll_foreach(backend->seats, it)
        seat_destroy(&it->item);
    tll_free(backend->seats);

    if (backend->layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(backend->layer_surface);
    if (backend->layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(backend->layer_shell);
    if (backend->surface != NULL)
        wl_surface_destroy(backend->surface);
    if (backend->compositor != NULL)
        wl_compositor_destroy(backend->compositor);
    if (backend->shm != NULL)
        wl_shm_destroy(backend->shm);
    if (backend->registry != NULL)
        wl_registry_destroy(backend->registry);
    if (backend->display != NULL) {
        wl_display_flush(backend->display);
        wl_display_disconnect(backend->display);
    }

    /* Destroyed when freeing buffer list */
    bar->pix = NULL;

}

static void
loop(struct bar *_bar,
     void (*expose)(const struct bar *bar),
     void (*on_mouse)(struct bar *bar, enum mouse_event event,
                      enum mouse_button btn, int x, int y))
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    pthread_setname_np(pthread_self(), "bar(wayland)");

    backend->bar_expose = expose;
    backend->bar_on_mouse = on_mouse;

    while (wl_display_prepare_read(backend->display) != 0)
        wl_display_dispatch_pending(backend->display);
    wl_display_flush(backend->display);

    while (true) {
        struct pollfd fds[] = {
            {.fd = _bar->abort_fd, .events = POLLIN},
            {.fd = wl_display_get_fd(backend->display), .events = POLLIN},
            {.fd = backend->pipe_fds[0], .events = POLLIN},
        };

        poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (fds[0].revents & POLLIN) {
            break;
        }

        if (fds[1].revents & POLLHUP) {
            LOG_INFO("disconnected from wayland");
            if (write(_bar->abort_fd, &(uint64_t){1}, sizeof(uint64_t))
                != sizeof(uint64_t))
            {
                LOG_ERRNO("failed to signal abort to modules");
            }
            break;
        }

        if (fds[2].revents & POLLIN) {
            uint8_t command;
            if (read(backend->pipe_fds[0], &command, sizeof(command))
                != sizeof(command))
            {
                LOG_ERRNO("failed to read from command pipe");
                break;
            }

            assert(command == 1);
            expose(_bar);
        }

        if (fds[1].revents & POLLIN) {
            wl_display_read_events(backend->display);

            while (wl_display_prepare_read(backend->display) != 0)
                wl_display_dispatch_pending(backend->display);
            wl_display_flush(backend->display);
        }
    }

    wl_display_cancel_read(backend->display);
}

static void
surface_enter(void *data, struct wl_surface *wl_surface,
              struct wl_output *wl_output)
{
    struct wayland_backend *backend = data;

    tll_foreach(backend->monitors, it) {
        struct monitor *mon = &it->item;

        if (mon->output != wl_output)
            continue;

        if (backend->monitor != mon) {
            backend->monitor = mon;

            int old_scale = backend->scale;
            update_size(backend);

            if (backend->scale != old_scale)
                refresh(backend->bar);
        }
        break;
    }
}

static void
surface_leave(void *data, struct wl_surface *wl_surface,
              struct wl_output *wl_output)
{
    struct wayland_backend *backend = data;
    backend->monitor = NULL;
}

static const struct wl_surface_listener surface_listener = {
    .enter = &surface_enter,
    .leave = &surface_leave,
};

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

        wl_surface_set_buffer_scale(backend->surface, backend->scale);
        wl_surface_attach(backend->surface, buffer->wl_buf, 0, 0);
        wl_surface_damage(backend->surface, 0, 0, backend->width, backend->height);

        struct wl_callback *cb = wl_surface_frame(backend->surface);
        wl_callback_add_listener(cb, &frame_listener, bar);
        wl_surface_commit(backend->surface);
        wl_display_flush(backend->display);

        backend->pending_buffer = NULL;
        backend->render_scheduled = true;
    } else
        ;//printf("nothing more to do\n");
}

static void
commit(const struct bar *_bar)
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

        wl_surface_set_buffer_scale(backend->surface, backend->scale);
        wl_surface_attach(backend->surface, buffer->wl_buf, 0, 0);
        wl_surface_damage(backend->surface, 0, 0, backend->width, backend->height);

        struct wl_callback *cb = wl_surface_frame(backend->surface);
        wl_callback_add_listener(cb, &frame_listener, bar);
        wl_surface_commit(backend->surface);
        wl_display_flush(backend->display);

        backend->render_scheduled = true;
    }

    backend->next_buffer = get_buffer(backend);
    assert(backend->next_buffer != NULL && backend->next_buffer->busy);
    bar->pix = backend->next_buffer->pix;
}

static void
refresh(const struct bar *_bar)
{
    const struct private *bar = _bar->private;
    const struct wayland_backend *backend = bar->backend.data;

    if (write(backend->pipe_fds[1], &(uint8_t){1}, sizeof(uint8_t))
        != sizeof(uint8_t))
    {
        LOG_ERRNO("failed to signal 'refresh' to main thread");
    }
}

static void
set_cursor(struct bar *_bar, const char *cursor)
{
    struct private *bar = _bar->private;
    struct wayland_backend *backend = bar->backend.data;

    struct seat *seat = backend->active_seat;
    if (seat == NULL || seat->pointer.theme == NULL)
        return;

    if (seat->pointer.xcursor != NULL && strcmp(seat->pointer.xcursor, cursor) == 0)
        return;

    seat->pointer.xcursor = cursor;

    seat->pointer.cursor = wl_cursor_theme_get_cursor(
        seat->pointer.theme, cursor);

    if (seat->pointer.cursor == NULL) {
        LOG_ERR("%s: failed to load cursor '%s'", seat->name, cursor);
        return;
    }

    update_cursor_surface(backend, seat);
}

const struct backend wayland_backend_iface = {
    .setup = &setup,
    .cleanup = &cleanup,
    .loop = &loop,
    .commit = &commit,
    .refresh = &refresh,
    .set_cursor = &set_cursor,
};
