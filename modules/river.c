#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <poll.h>

#include <wayland-client.h>
#include <tllist.h>

#define LOG_MODULE "river"
#define LOG_ENABLE_DBG 1
#include "../log.h"
#include "../plugin.h"
#include "../particles/dynlist.h"

#include "river-status-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

struct private;

struct output {
    struct private *m;
    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;
    struct zriver_output_status_v1 *status;
    uint32_t wl_name;
    char *name;

    /* Tags */
    uint32_t occupied;
    uint32_t focused;
};

struct seat {
    struct private *m;
    struct wl_seat *wl_seat;
    struct zriver_seat_status_v1 *status;
    uint32_t wl_name;
    char *name;

    char *title;
    struct output *output;
};

struct private {
    struct module *mod;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct zriver_status_manager_v1 *status_manager;
    struct particle *template;
    struct particle *title;

    bool is_starting_up;
    tll(struct output) outputs;
    tll(struct seat) seats;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->template->destroy(m->template);
    if (m->title != NULL)
        m->title->destroy(m->title);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&m->mod->lock);

    uint32_t output_focused = 0;
    uint32_t seat_focused = 0;
    uint32_t occupied = 0;

    tll_foreach(m->outputs, it) {
        const struct output *output = &it->item;

        output_focused |= output->focused;
        occupied |= output->occupied;

        tll_foreach(m->seats, it2) {
            const struct seat *seat = &it2->item;
            if (seat->output == output) {
                seat_focused |= output->focused;
            }
        }
    }

    const size_t seat_count = m->title != NULL ? tll_length(m->seats) : 0;
    struct exposable *tag_parts[32 + seat_count];

    for (unsigned i = 0; i < 32; i++) {
        /* It's visible if any output has it focused */
        bool visible = output_focused & (1u << i);

        /* It's focused if any output that has seat focus has it focused */
        bool focused = seat_focused & (1u << i);

        const char *state = visible ? focused ? "focused" : "unfocused" : "invisible";

#if 0
        LOG_DBG("tag: #%u, visible=%d, focused=%d, occupied=%d, state=%s",
                i, visible, focused, occupied & (1u << i), state);
#endif

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_int(mod, "id", i + 1),
                tag_new_bool(mod, "visible", visible),
                tag_new_bool(mod, "focused", focused),
                tag_new_bool(mod, "occupied", occupied & (1u << i)),
                tag_new_string(mod, "state", state),
            },
            .count = 5,
        };

        tag_parts[i] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    if (m->title != NULL) {
        size_t i = 32;
        tll_foreach(m->seats, it) {
            const struct seat *seat = &it->item;

            struct tag_set tags = {
                .tags = (struct tag *[]){
                    tag_new_string(mod, "seat", seat->name),
                    tag_new_string(mod, "title", seat->title),
                },
                .count = 2,
            };

            tag_parts[i++] = m->title->instantiate(m->title, &tags);
            tag_set_destroy(&tags);
        }
    }
    
    mtx_unlock(&m->mod->lock);
    return dynlist_exposable_new(tag_parts, 32 + seat_count, 0, 0);
}

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
output_destroy(struct output *output)
{
    free(output->name);
    if (output->status != NULL)
        zriver_output_status_v1_destroy(output->status);
    if (output->xdg_output != NULL)
        zxdg_output_v1_destroy(output->xdg_output);
    if (output->wl_output != NULL)
        wl_output_destroy(output->wl_output);
}

static void
seat_destroy(struct seat *seat)
{
    free(seat->title);
    free(seat->name);
    if (seat->status != NULL)
        zriver_seat_status_v1_destroy(seat->status);
    if (seat->wl_seat != NULL)
        wl_seat_destroy(seat->wl_seat);
}

static void
focused_tags(void *data, struct zriver_output_status_v1 *zriver_output_status_v1,
             uint32_t tags)
{
    struct output *output = data;
    struct module *mod = output->m->mod;

    LOG_DBG("output: %s: focused tags: 0x%08x", output->name, tags);

    mtx_lock(&mod->lock);
    {
        output->focused = tags;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static void
view_tags(void *data, struct zriver_output_status_v1 *zriver_output_status_v1,
          struct wl_array *tags)
{
    struct output *output = data;
    struct module *mod = output->m->mod;

    mtx_lock(&mod->lock);
    {
        output->occupied = 0;

        /* Each entry in the list is a view, and the value is the tags
         * associated with that view */
        uint32_t *set;
        wl_array_for_each(set, tags) {
            output->occupied |= *set;
        }
    
        LOG_DBG("output: %s: occupied tags: 0x%0x", output->name, output->occupied);
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static const struct zriver_output_status_v1_listener river_status_output_listener = {
    .focused_tags = &focused_tags,
    .view_tags = &view_tags,
};

static void
xdg_output_handle_logical_position(void *data,
                                   struct zxdg_output_v1 *xdg_output,
                                   int32_t x, int32_t y)
{
}

static void
xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                               int32_t width, int32_t height)
{
}

static void
xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
                       const char *name)
{
    struct output *output = data;
    struct module *mod = output->m->mod;

    mtx_lock(&mod->lock);
    {
        free(output->name);
        output->name = name != NULL ? strdup(name) : NULL;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
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
instantiate_output(struct output *output)
{
    if (output->m->is_starting_up)
        return;

    assert(output->wl_output != NULL);

    if (output->m->status_manager != NULL && output->status == NULL) {
        output->status = zriver_status_manager_v1_get_river_output_status(
            output->m->status_manager, output->wl_output);

        if (output->status != NULL) {
            zriver_output_status_v1_add_listener(
                output->status, &river_status_output_listener, output);
        }
    }

    if (output->m->xdg_output_manager != NULL && output->xdg_output == NULL) {
        output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
            output->m->xdg_output_manager, output->wl_output);

        if (output->xdg_output != NULL) {
            zxdg_output_v1_add_listener(
                output->xdg_output, &xdg_output_listener, output);
        }
    }
}

static void
focused_output(void *data, struct zriver_seat_status_v1 *zriver_seat_status_v1,
               struct wl_output *wl_output)
{
    struct seat *seat = data;
    struct private *m = seat->m;
    struct module *mod = m->mod;

    mtx_lock(&mod->lock);
    {
        struct output *output = NULL;
        tll_foreach(m->outputs, it) {
            if (it->item.wl_output == wl_output) {
                output = &it->item;
                break;
            }
        }

        LOG_DBG("seat: %s: focused output: %s", seat->name, output != NULL ? output->name : "<unknown>");

        if (output == NULL)
            LOG_WARN("seat: %s: couldn't find output we are mapped on", seat->name);

        seat->output = output;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static void
unfocused_output(void *data, struct zriver_seat_status_v1 *zriver_seat_status_v1,
				 struct wl_output *wl_output)
{
    struct seat *seat = data;
    struct private *m = seat->m;
    struct module *mod = m->mod;

    mtx_lock(&mod->lock);
    {

        struct output *output = NULL;
        tll_foreach(m->outputs, it) {
            if (it->item.wl_output == wl_output) {
                output = &it->item;
                break;
            }
        }

        LOG_DBG("seat: %s: unfocused output: %s", seat->name, output != NULL ? output->name : "<unknown>");
        if (output == NULL)
            LOG_WARN("seat: %s: couldn't find output we were unmapped from", seat->name);

        seat->output = NULL;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static void
focused_view(void *data, struct zriver_seat_status_v1 *zriver_seat_status_v1,
             const char *title)
{
    struct seat *seat = data;
    struct module *mod = seat->m->mod;

    LOG_DBG("seat: %s: focused view: %s", seat->name, title);

    mtx_lock(&mod->lock);
    {
        free(seat->title);
        seat->title = title != NULL ? strdup(title) : NULL;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static const struct zriver_seat_status_v1_listener river_seat_status_listener = {
    .focused_output = &focused_output,
    .unfocused_output = &unfocused_output,
    .focused_view = &focused_view,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
}

static void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
    struct seat *seat = data;
    struct module *mod = seat->m->mod;

    mtx_lock(&mod->lock);
    {
        free(seat->name);
        seat->name = name != NULL ? strdup(name) : NULL;
    }
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void
instantiate_seat(struct seat *seat)
{
    assert(seat->wl_seat != NULL);

    if (seat->m->is_starting_up)
        return;

    if (seat->m->status_manager == NULL)
        return;

    seat->status = zriver_status_manager_v1_get_river_seat_status(
        seat->m->status_manager, seat->wl_seat);

    if (seat->status == NULL)
        return;

    zriver_seat_status_v1_add_listener(
        seat->status, &river_seat_status_listener, seat);
}

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct private *m = data;

    if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_output *wl_output = wl_registry_bind(
            registry, name, &wl_output_interface, required);

        if (wl_output == NULL)
            return;

        mtx_lock(&m->mod->lock);
        tll_push_back(m->outputs, ((struct output){.m = m, .wl_output = wl_output, .wl_name = name}));
        instantiate_output(&tll_back(m->outputs));
        mtx_unlock(&m->mod->lock);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        m->xdg_output_manager = wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, required);

        mtx_lock(&m->mod->lock);
        tll_foreach(m->outputs, it)
            instantiate_output(&it->item);
        mtx_unlock(&m->mod->lock);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_seat *wl_seat = wl_registry_bind(
            registry, name, &wl_seat_interface, required);

        if (wl_seat == NULL)
            return;

        mtx_lock(&m->mod->lock);
        tll_push_back(m->seats, ((struct seat){.m = m, .wl_seat = wl_seat, .wl_name = name}));
        struct seat *seat = &tll_back(m->seats);

        wl_seat_add_listener(wl_seat, &seat_listener, seat);
        instantiate_seat(seat);
        mtx_unlock(&m->mod->lock);
    }

    else if (strcmp(interface, zriver_status_manager_v1_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        m->status_manager = wl_registry_bind(
            registry, name, &zriver_status_manager_v1_interface, required);

        mtx_lock(&m->mod->lock);
        tll_foreach(m->outputs, it)
            instantiate_output(&it->item);
        tll_foreach(m->seats, it)
            instantiate_seat(&it->item);
        mtx_unlock(&m->mod->lock);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct private *m = data;

    mtx_lock(&m->mod->lock);
    tll_foreach(m->outputs, it) {
        if (it->item.wl_name == name) {
            output_destroy(&it->item);
            tll_remove(m->outputs, it);
            mtx_unlock(&m->mod->lock);
            return;
        }
    }

    tll_foreach(m->seats, it) {
        if (it->item.wl_name == name) {
            seat_destroy(&it->item);
            tll_remove(m->seats, it);
            mtx_unlock(&m->mod->lock);
            return;
        }
    }

    mtx_unlock(&m->mod->lock);
}

static const struct wl_registry_listener registry_listener = {
    .global = &handle_global,
    .global_remove = &handle_global_remove,
};

static int
run(struct module *mod)
{
    struct private *m = mod->private;

    int ret = 1;
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;

    if ((display = wl_display_connect(NULL)) == NULL) {
        LOG_ERR("no Wayland compositor running?");
        goto out;
    }

    if ((registry = wl_display_get_registry(display)) == NULL ||
        wl_registry_add_listener(registry, &registry_listener, m) != 0)
    {
        LOG_ERR("failed to get Wayland registry");
        goto out;
    }

    wl_display_roundtrip(display);

    if (m->status_manager == NULL) {
        LOG_ERR("river does not appear to be running");
        goto out;
    }

    wl_display_roundtrip(display);
    m->is_starting_up = false;

    tll_foreach(m->outputs, it)
        instantiate_output(&it->item);
    tll_foreach(m->seats, it)
        instantiate_seat(&it->item);

    while (true) {
        wl_display_flush(display);

        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = wl_display_get_fd(display), .events = POLLIN},
        };

        int r = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (r == -1) {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("failed to poll");
            break;
        }

        if ((fds[0].revents & POLLIN) || (fds[0].revents & POLLHUP))
            break;

        if (fds[1].revents & POLLHUP) {
            LOG_ERRNO("disconnected from Wayland compositor");
            break;
        }

        assert(fds[1].revents & POLLIN);
        wl_display_dispatch(display);
    }

    ret = 0;
out:
    tll_foreach(m->seats, it)
        seat_destroy(&it->item);
    tll_free(m->seats);
    tll_foreach(m->outputs, it)
        output_destroy(&it->item);
    tll_free(m->outputs);

    if (m->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(m->xdg_output_manager);
    if (m->status_manager != NULL)
        zriver_status_manager_v1_destroy(m->status_manager);
    if (registry != NULL)
        wl_registry_destroy(registry);
    if (display != NULL)
        wl_display_disconnect(display);
    return ret;
}

static struct module *
river_new(struct particle *template, struct particle *title)
{
    struct private *m = calloc(1, sizeof(*m));
    m->template = template;
    m->title = title;
    m->is_starting_up = true;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    m->mod = mod;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    const struct yml_node *title = yml_get_value(node, "title");
    return river_new(
        conf_to_particle(c, inherited),
        title != NULL ? conf_to_particle(title, inherited) : NULL);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"title", false, &conf_verify_particle},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_river_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_river_iface")));
#endif
