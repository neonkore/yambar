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
#include "xdg-output-unstable-v1.h"

#define min(x, y) ((x) < (y) ? (x) : (y))

static const int required_manager_interface_version = 2;

struct output {
    struct module *mod;

    uint32_t wl_name;
    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;

    char *name;
    bool use_output_release;
};

struct toplevel {
    struct module *mod;
    struct zwlr_foreign_toplevel_handle_v1 *handle;

    char *app_id;
    char *title;

    bool maximized;
    bool minimized;
    bool activated;
    bool fullscreen;

    tll(const struct output *) outputs;
};

struct private {
    struct particle *template;
    uint32_t manager_wl_name;
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct zxdg_output_manager_v1 *xdg_output_manager;

    bool all_monitors;
    tll(struct toplevel) toplevels;
    tll(struct output) outputs;
};

static void
output_free(struct output *output)
{
    free(output->name);
    if (output->xdg_output != NULL)
        zxdg_output_v1_destroy(output->xdg_output);
    if (output->wl_output != NULL) {
        if (output->use_output_release)
            wl_output_release(output->wl_output);
        else
            wl_output_destroy(output->wl_output);
    }
}

static void
toplevel_free(struct toplevel *top)
{
    if (top->handle != NULL)
        zwlr_foreign_toplevel_handle_v1_destroy(top->handle);

    free(top->app_id);
    free(top->title);
    tll_free(top->outputs);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->template->destroy(m->template);

    assert(tll_length(m->toplevels) == 0);
    assert(tll_length(m->outputs) == 0);

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

    const size_t toplevel_count = tll_length(m->toplevels);
    size_t show_count = 0;
    struct exposable *toplevels[toplevel_count];

    const char *current_output = mod->bar->output_name(mod->bar);

    tll_foreach(m->toplevels, it) {
        const struct toplevel *top = &it->item;

        bool show = false;

        if (m->all_monitors)
            show = true;
        else if (current_output != NULL) {
            tll_foreach(top->outputs, it2) {
                const struct output *output = it2->item;
                if (output->name != NULL &&
                    strcmp(output->name, current_output) == 0)
                {
                    show = true;
                    break;
                }
            }
        }

        if (!show)
            continue;

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

        toplevels[show_count++] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(toplevels, show_count, 0, 0);
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
    struct module *mod = output->mod;

    mtx_lock(&mod->lock);
    {
        free(output->name);
        output->name = name != NULL ? strdup(name) : NULL;
    }
    mtx_unlock(&mod->lock);
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
output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
             struct wl_output *wl_output)
{
    struct toplevel *top = data;
    struct module *mod = top->mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const struct output *output = NULL;
    tll_foreach(m->outputs, it) {
        if (it->item.wl_output == wl_output) {
            output = &it->item;
            break;
        }
    }

    if (output == NULL) {
        LOG_ERR("output-enter event on untracked output");
        goto out;
    }

    tll_foreach(top->outputs, it) {
        if (it->item == output) {
            LOG_ERR("output-enter event on output we're already on");
            goto out;
        }
    }

    LOG_DBG("mapped: %s:%s on %s", top->app_id, top->title, output->name);
    tll_push_back(top->outputs, output);

out:
    mtx_unlock(&mod->lock);
}

static void
output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
             struct wl_output *wl_output)
{
    struct toplevel *top = data;
    struct module *mod = top->mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const struct output *output = NULL;
    tll_foreach(m->outputs, it) {
        if (it->item.wl_output == wl_output) {
            output = &it->item;
            break;
        }
    }

    if (output == NULL) {
        LOG_ERR("output-leave event on untracked output");
        goto out;
    }

    bool output_removed = false;
    tll_foreach(top->outputs, it) {
        if (it->item == output) {
            LOG_DBG("unmapped: %s:%s from %s",
                    top->app_id, top->title, output->name);
            tll_remove(top->outputs, it);
            output_removed = true;
            break;
        }
    }

    if (!output_removed) {
        LOG_ERR("output-leave event on an output we're not on");
        goto out;
    }

out:
    mtx_unlock(&mod->lock);
}

static void
state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
      struct wl_array *states)
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
    struct module *mod = top->mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);
    tll_foreach(m->toplevels, it) {
        if (it->item.handle == handle) {
            toplevel_free(top);
            tll_remove(m->toplevels, it);
            break;
        }
    }
    mtx_unlock(&mod->lock);
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

    mtx_lock(&mod->lock);
    {
        tll_push_back(m->toplevels, toplevel);

        zwlr_foreign_toplevel_handle_v1_add_listener(
            handle, &toplevel_listener, &tll_back(m->toplevels));
    }
    mtx_unlock(&mod->lock);
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
output_xdg_output(struct output *output)
{
    struct private *m = output->mod->private;

    if (m->xdg_output_manager == NULL)
        return;
    if (output->xdg_output != NULL)
        return;

    output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
        m->xdg_output_manager, output->wl_output);
    zxdg_output_v1_add_listener(
        output->xdg_output, &xdg_output_listener, output);
}

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct module *mod = data;
    struct private *m = mod->private;

    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        if (!verify_iface_version(interface, version, required_manager_interface_version))
            return;

        m->manager_wl_name = name;
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        struct output output = {
            .mod = mod,
            .wl_name = name,
            .wl_output = wl_registry_bind(
                registry, name,
                &wl_output_interface, min(version, WL_OUTPUT_RELEASE_SINCE_VERSION)),
            .use_output_release = version >= WL_OUTPUT_RELEASE_SINCE_VERSION,
        };

        mtx_lock(&mod->lock);
        tll_push_back(m->outputs, output);
        output_xdg_output(&tll_back(m->outputs));
        mtx_unlock(&mod->lock);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        m->xdg_output_manager = wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, required);

        mtx_lock(&mod->lock);
        tll_foreach(m->outputs, it)
            output_xdg_output(&it->item);
        mtx_unlock(&mod->lock);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct module *mod = data;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    tll_foreach(m->outputs, it) {
        const struct output *output = &it->item;
        if (output->wl_name == name) {

            /* Loop all toplevels */
            tll_foreach(m->toplevels, it2) {

                /* And remove this output from their list of tracked
                 * outputs */
                tll_foreach(it2->item.outputs, it3) {
                    if (it3->item == output) {
                        tll_remove(it2->item.outputs, it3);
                        break;
                    }
                }
            }

            tll_remove(m->outputs, it);
            goto out;
        }
    }

out:
    mtx_unlock(&mod->lock);
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

    if (m->manager_wl_name == 0) {
        LOG_ERR(
            "compositor does not implement the foreign-toplevel-manager interface");
        goto out;
    }

    m->manager = wl_registry_bind(
        registry, m->manager_wl_name,
        &zwlr_foreign_toplevel_manager_v1_interface,
        required_manager_interface_version);

    zwlr_foreign_toplevel_manager_v1_add_listener(
        m->manager, &manager_listener, mod);

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

    tll_foreach(m->outputs, it) {
        output_free(&it->item);
        tll_remove(m->outputs, it);
    }

    if (m->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(m->xdg_output_manager);
    if (m->manager != NULL)
        zwlr_foreign_toplevel_manager_v1_destroy(m->manager);
    if (registry != NULL)
        wl_registry_destroy(registry);
    if (display != NULL)
        wl_display_disconnect(display);
    return ret;
}

static struct module *
ftop_new(struct particle *label, bool all_monitors)
{
    struct private *m = calloc(1, sizeof(*m));
    m->template = label;
    m->all_monitors = all_monitors;

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
    const struct yml_node *all_monitors = yml_get_value(node, "all-monitors");

    return ftop_new(
        conf_to_particle(c, inherited),
        all_monitors != NULL ? yml_value_as_bool(all_monitors) : false);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"all-monitors", false, &conf_verify_bool},
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
