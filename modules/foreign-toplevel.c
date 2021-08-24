#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <poll.h>

#include <tllist.h>
#include <wayland-client.h>

#define LOG_MODULE "foreign-toplevel"
#define LOG_ENABLE_DBG 1
#include "../log.h"
#include "../plugin.h"
#include "../particles/dynlist.h"

#include "wlr-foreign-toplevel-management-unstable-v1.h"

struct toplevel {
    struct module *mod;
    struct zwlr_foreign_toplevel_handle_v1 *handle;

    char *app_id;
    char *title;

    bool maximized;
    bool minimized;
    bool activated;
    bool fullscreen;
};

struct private {
    struct particle *template;
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    tll(struct toplevel) toplevels;
};

static void
toplevel_free(struct toplevel *top)
{
    if (top->handle != NULL)
        zwlr_foreign_toplevel_handle_v1_destroy(top->handle);

    free(top->app_id);
    free(top->title);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->template->destroy(m->template);

    tll_foreach(m->toplevels, it) {
        toplevel_free(&it->item);
        tll_remove(m->toplevels, it);
    }

    free(m);
    module_default_destroy(mod);
}

static const char *
description(struct module *mod)
{
    return "toplevel";
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    size_t toplevel_count = tll_length(m->toplevels);
    struct exposable *toplevels[toplevel_count];

    size_t idx = 0;
    tll_foreach(m->toplevels, it) {
        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "app-id", it->item.app_id),
                tag_new_string(mod, "title", it->item.title),
                tag_new_bool(mod, "maximized", it->item.maximized),
                tag_new_bool(mod, "minimized", it->item.minimized),
                tag_new_bool(mod, "activated", it->item.activated),
                tag_new_bool(mod, "fullscreen", it->item.fullscreen),
            },
            .count = 6,
        };

        toplevels[idx++] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(toplevels, toplevel_count, 0, 0);
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
title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title)
{
    struct toplevel *top = data;

    mtx_lock(&top->mod->lock);
    {
        free(top->title);
        top->title = title != NULL ? strdup(title) : NULL;
    }
    mtx_unlock(&top->mod->lock);
}

static void
app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id)
{
    struct toplevel *top = data;

    mtx_lock(&top->mod->lock);
    {
        free(top->app_id);
        top->app_id = app_id != NULL ? strdup(app_id) : NULL;
    }
    mtx_unlock(&top->mod->lock);
}

static void
output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output)
{
}

static void
output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output)
{
}

static void
state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *states)
{
    struct toplevel *top = data;

    bool maximized = false;
    bool minimized = false;
    bool activated = false;
    bool fullscreen = false;

    enum zwlr_foreign_toplevel_handle_v1_state *state;
    wl_array_for_each(state, states) {
        switch (*state) {
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED: maximized = true; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED: minimized = true; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED: activated = true; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: fullscreen = true; break;
        }
    }

    mtx_lock(&top->mod->lock);
    {
        top->maximized = maximized;
        top->minimized = minimized;
        top->activated = activated;
        top->fullscreen = fullscreen;
    }
    mtx_unlock(&top->mod->lock);
}

static void
done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    struct toplevel *top = data;
    const struct bar *bar = top->mod->bar;
    bar->refresh(bar);
}

static void
closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    struct toplevel *top = data;
    struct private *m = top->mod->private;

    tll_foreach(m->toplevels, it) {
        if (it->item.handle == handle) {
            toplevel_free(top);
            tll_remove(m->toplevels, it);
            break;
        }
    }
}

static void
parent(void *data,
       struct zwlr_foreign_toplevel_handle_v1 *handle,
       struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = &title,
    .app_id = &app_id,
    .output_enter = &output_enter,
    .output_leave = &output_leave,
    .state = &state,
    .done = &done,
    .closed = &closed,
    .parent = &parent,
};

static void
toplevel(void *data,
         struct zwlr_foreign_toplevel_manager_v1 *manager,
         struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    struct module *mod = data;
    struct private *m = mod->private;

    struct toplevel toplevel = {
        .mod = mod,
        .handle = handle,
    };

    tll_push_back(m->toplevels, toplevel);

    zwlr_foreign_toplevel_handle_v1_add_listener(
        handle, &toplevel_listener, &tll_back(m->toplevels));
}

static void
finished(void *data,
         struct zwlr_foreign_toplevel_manager_v1 *manager)
{
    struct module *mod = data;
    struct private *m = mod->private;

    assert(m->manager == manager);
    zwlr_foreign_toplevel_manager_v1_destroy(m->manager);
    m->manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = &toplevel,
    .finished = &finished,
};

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct module *mod = data;
    struct private *m = mod->private;

    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        m->manager = wl_registry_bind(
            registry, name,
            &zwlr_foreign_toplevel_manager_v1_interface, required);

        zwlr_foreign_toplevel_manager_v1_add_listener(
            m->manager, &manager_listener, mod);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = &handle_global,
    .global_remove = &handle_global_remove,
};

static int
run(struct module *mod)
{
    struct private *m = mod->private;
    int ret = -1;

    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;

    if ((display = wl_display_connect(NULL)) == NULL) {
        LOG_ERR("no Wayland compositor running");
        goto out;
    }

    if ((registry = wl_display_get_registry(display)) == NULL ||
        wl_registry_add_listener(registry, &registry_listener, mod) != 0)
    {
        LOG_ERR("failed to get Wayland registry");
        goto out;
    }

    wl_display_roundtrip(display);

    while (true) {
        wl_display_flush(display);

        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = wl_display_get_fd(display), .events = POLLIN},
        };

        int r = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("failed to poll");
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ret = 0;
            break;
        }

        if (fds[1].revents & POLLHUP) {
            LOG_ERR("disconnected from the Wayland compositor");
            break;
        }

        assert(fds[1].revents & POLLIN);
        wl_display_dispatch(display);
    }

out:
    tll_foreach(m->toplevels, it) {
        toplevel_free(&it->item);
        tll_remove(m->toplevels, it);
    }

    if (m->manager != NULL)
        zwlr_foreign_toplevel_manager_v1_destroy(m->manager);
    if (registry != NULL)
        wl_registry_destroy(registry);
    if (display != NULL)
        wl_display_disconnect(display);
    return ret;
}

static struct module *
ftop_new(struct particle *label)
{
    struct private *m = calloc(1, sizeof(*m));
    m->template = label;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return ftop_new(conf_to_particle(c, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_foreign_toplevel_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_foreign_toplevel_iface")));
#endif
