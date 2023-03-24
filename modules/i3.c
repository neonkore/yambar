#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>

#include <sys/types.h>
#include <fcntl.h>

#include <tllist.h>

#define LOG_MODULE "i3"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

#include "i3-ipc.h"
#include "i3-common.h"

enum sort_mode {SORT_NONE, SORT_NATIVE, SORT_ASCENDING, SORT_DESCENDING};

struct ws_content {
    char *name;
    struct particle *content;
};

struct workspace {
    int id;
    char *name;
    int name_as_int; /* -1 if name is not a decimal number */
    bool persistent;

    char *output;
    bool visible;
    bool focused;
    bool urgent;
    bool empty;

    struct {
        unsigned id;
        char *title;
        char *application;
        pid_t pid;
    } window;
};

struct private {
    int left_spacing;
    int right_spacing;

    bool dirty;

    char *mode;

    struct {
        struct ws_content *v;
        size_t count;
    } ws_content;

    bool strip_workspace_numbers;
    enum sort_mode sort_mode;
    tll(struct workspace) workspaces;

    size_t persistent_count;
    char **persistent_workspaces;
};

static int
workspace_name_as_int(const char *name)
{
    int name_as_int = 0;

    /* First check for N:name pattern (set $ws1 “1:foobar”) */
    const char *colon = strchr(name, ':');
    if (colon != NULL) {
        for (const char *p = name; p < colon; p++) {
            if (!(*p >= '0' && *p < '9'))
                return -1;

            name_as_int *= 10;
            name_as_int += *p - '0';
        }

        return name_as_int;
    }

    /* Then, if the name is a number *only* (set $ws1 1) */
    for (const char *p = name; *p != '\0'; p++) {
        if (!(*p >= '0' && *p <= '9'))
            return -1;

        name_as_int *= 10;
        name_as_int += *p - '0';
    }

    return name_as_int;
}

static bool
workspace_from_json(const struct json_object *json, struct workspace *ws)
{
    /* Always present */
    struct json_object *id, *name, *output;
    if (!json_object_object_get_ex(json, "id", &id) ||
        !json_object_object_get_ex(json, "name", &name) ||
        !json_object_object_get_ex(json, "output", &output))
    {
        LOG_ERR("workspace reply/event without 'name' and/or 'output' "
                "properties");
        return false;
    }

    /* Sway only */
    struct json_object *focus = NULL;
    json_object_object_get_ex(json, "focus", &focus);

    /* Optional */
    struct json_object *visible = NULL, *focused = NULL, *urgent = NULL;
    json_object_object_get_ex(json, "visible", &visible);
    json_object_object_get_ex(json, "focused", &focused);
    json_object_object_get_ex(json, "urgent", &urgent);

    const char *name_as_string = json_object_get_string(name);

    const size_t node_count = focus != NULL
        ? json_object_array_length(focus)
        : 0;

    const bool is_empty = node_count == 0;
    int name_as_int = workspace_name_as_int(name_as_string);

    *ws = (struct workspace) {
        .id = json_object_get_int(id),
        .name = strdup(name_as_string),
        .name_as_int = name_as_int,
        .persistent = false,
        .output = strdup(json_object_get_string(output)),
        .visible = json_object_get_boolean(visible),
        .focused = json_object_get_boolean(focused),
        .urgent = json_object_get_boolean(urgent),
        .empty = is_empty && json_object_get_boolean(focused),
        .window = {.title = NULL, .pid = -1},
    };

    return true;
}

static void
workspace_free_persistent(struct workspace *ws)
{
    free(ws->output); ws->output = NULL;
    free(ws->window.title); ws->window.title = NULL;
    free(ws->window.application); ws->window.application = NULL;
    ws->id = -1;
}

static void
workspace_free(struct workspace *ws)
{
    workspace_free_persistent(ws);
    free(ws->name); ws->name = NULL;
}

static void
workspaces_free(struct private *m, bool free_persistent)
{
    tll_foreach(m->workspaces, it) {
        if (free_persistent || !it->item.persistent) {
            workspace_free(&it->item);
            tll_remove(m->workspaces, it);
        }
    }
}


static void
workspace_add(struct private *m, struct workspace ws)
{
    switch (m->sort_mode) {
    case SORT_NONE:
        tll_push_back(m->workspaces, ws);
        return;

    case SORT_NATIVE:
        if (ws.name_as_int >= 0) {
            tll_foreach(m->workspaces, it) {
                if (it->item.name_as_int < 0)
                    continue;
                if (it->item.name_as_int > ws.name_as_int) {
                    tll_insert_before(m->workspaces, it, ws);
                    return;
                }
            }
        };

        tll_push_back(m->workspaces, ws);
        return;

    case SORT_ASCENDING:
        if (ws.name_as_int >= 0) {
            tll_foreach(m->workspaces, it) {
                if (it->item.name_as_int < 0)
                    continue;
                if (it->item.name_as_int > ws.name_as_int) {
                    tll_insert_before(m->workspaces, it, ws);
                    return;
                }
            }
        } else {
            tll_foreach(m->workspaces, it) {
                if (strcoll(it->item.name, ws.name) > 0 ||
                    it->item.name_as_int >= 0)
                {
                    tll_insert_before(m->workspaces, it, ws);
                    return;
                }
            }
        }
        tll_push_back(m->workspaces, ws);
        return;

    case SORT_DESCENDING:
        if (ws.name_as_int >= 0) {
            tll_foreach(m->workspaces, it) {
                if (it->item.name_as_int < ws.name_as_int) {
                    tll_insert_before(m->workspaces, it, ws);
                    return;
                }
            }
        } else {
            tll_foreach(m->workspaces, it) {
                if (it->item.name_as_int >= 0)
                    continue;
                if (strcoll(it->item.name, ws.name) < 0) {
                    tll_insert_before(m->workspaces, it, ws);
                    return;
                }
            }
        }
        tll_push_back(m->workspaces, ws);
        return;
    }
}

static void
workspace_del(struct private *m, int id)
{
    tll_foreach(m->workspaces, it) {
        struct workspace *ws = &it->item;

        if (ws->id != id)
            continue;

        workspace_free(ws);
        tll_remove(m->workspaces, it);
        break;
    }
}

static struct workspace *
workspace_lookup(struct private *m, int id)
{
    tll_foreach(m->workspaces, it) {
        struct workspace *ws = &it->item;
        if (ws->id == id)
            return ws;
    }
    return NULL;
}

static struct workspace *
workspace_lookup_by_name(struct private *m, const char *name)
{
    tll_foreach(m->workspaces, it) {
        struct workspace *ws = &it->item;
        if (strcmp(ws->name, name) == 0)
            return ws;
    }
    return NULL;
}

static bool
handle_get_version_reply(int sock, int type, const struct json_object *json, void *_m)
{
    struct json_object *version;
    if (!json_object_object_get_ex(json, "human_readable", &version)) {
        LOG_ERR("version reply without 'humand_readable' property");
        return false;
    }

    LOG_INFO("i3: %s", json_object_get_string(version));
    return true;
}

static bool
handle_subscribe_reply(int sock, int type, const struct json_object *json, void *_m)
{
    struct json_object *success;
    if (!json_object_object_get_ex(json, "success", &success)) {
        LOG_ERR("subscribe reply without 'success' property");
        return false;
    }

    if (!json_object_get_boolean(success)) {
        LOG_ERR("failed to subscribe");
        return false;
    }

    return true;
}

static bool
workspace_update_or_add(struct private *m, const struct json_object *ws_json)
{
    struct json_object *_id;
    if (!json_object_object_get_ex(ws_json, "id", &_id))
        return false;

    const int id = json_object_get_int(_id);
    struct workspace *already_exists = workspace_lookup(m, id);

    if (already_exists == NULL) {
        /*
         * No workspace with this ID.
         *
         * Try looking it up again, but this time using the name. If
         * we get a match, check if it’s an empty, persistent
         * workspace, and if so, use it.
         *
         * This is necessary, since empty, persistent workspaces don’t
         * exist in the i3/Sway server, and thus we don’t _have_ an
         * ID.
         */
        struct json_object *_name;
        if (json_object_object_get_ex(ws_json, "name", &_name)) {
            const char *name = json_object_get_string(_name);
            if (name != NULL) {
                struct workspace *maybe_persistent =
                    workspace_lookup_by_name(m, name);

                if (maybe_persistent != NULL &&
                    maybe_persistent->persistent &&
                    maybe_persistent->id < 0)
                {
                    already_exists = maybe_persistent;
                }
            }
        }
    }

    if (already_exists != NULL) {
        bool persistent = already_exists->persistent;
        assert(persistent);

        workspace_free(already_exists);
        if (!workspace_from_json(ws_json, already_exists))
            return false;
        already_exists->persistent = persistent;
    } else {
        struct workspace ws;
        if (!workspace_from_json(ws_json, &ws))
            return false;

        workspace_add(m, ws);
    }

    return true;
}

static bool
handle_get_workspaces_reply(int sock, int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    workspaces_free(m, false);
    m->dirty = true;

    size_t count = json_object_array_length(json);

    for (size_t i = 0; i < count; i++) {
        if (!workspace_update_or_add(m, json_object_array_get_idx(json, i)))
            goto err;
    }

    mtx_unlock(&mod->lock);
    return true;

err:
    workspaces_free(m, false);
    mtx_unlock(&mod->lock);
    return false;
}

static bool
handle_workspace_event(int sock, int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change)) {
        LOG_ERR("workspace event without 'change' property");
        return false;
    }

    const char *change_str = json_object_get_string(change);

    bool is_init = strcmp(change_str, "init") == 0;
    bool is_empty = strcmp(change_str, "empty") == 0;
    bool is_focused = strcmp(change_str, "focus") == 0;
    bool is_rename = strcmp(change_str, "rename") == 0;
    bool is_move = strcmp(change_str, "move") == 0;
    bool is_urgent = strcmp(change_str, "urgent") == 0;

    struct json_object *current, *_current_id;
    if (!json_object_object_get_ex(json, "current", &current) ||
        !json_object_object_get_ex(current, "id", &_current_id))
    {
        LOG_ERR("workspace event without 'current' and/or 'id' properties");
        return false;
    }

    int current_id = json_object_get_int(_current_id);

    mtx_lock(&mod->lock);

    if (is_init) {
        if (!workspace_update_or_add(m, current))
            goto err;
    }

    else if (is_empty) {
        struct workspace *ws = workspace_lookup(m, current_id);
        assert(ws != NULL);

        if (!ws->persistent)
            workspace_del(m, current_id);
        else {
            workspace_free_persistent(ws);
            ws->empty = true;
        }
    }

    else if (is_focused) {
        struct json_object *old, *_old_id, *urgent;
        if (!json_object_object_get_ex(json, "old", &old) ||
            !json_object_object_get_ex(old, "id", &_old_id) ||
            !json_object_object_get_ex(current, "urgent", &urgent))
        {
            LOG_ERR("workspace 'focused' event without 'old', 'name' and/or 'urgent' property");
            mtx_unlock(&mod->lock);
            return false;
        }

        struct workspace *w = workspace_lookup(m, current_id);
        assert(w != NULL);

        LOG_DBG("w: %s", w->name);

        /* Mark all workspaces on current's output invisible */
        tll_foreach(m->workspaces, it) {
            struct workspace *ws = &it->item;
            if (ws->output != NULL && strcmp(ws->output, w->output) == 0)
                ws->visible = false;
        }

        w->urgent = json_object_get_boolean(urgent);
        w->focused = true;
        w->visible = true;

        /* Old workspace is no longer focused */
        int old_id = json_object_get_int(_old_id);
        struct workspace *old_w = workspace_lookup(m, old_id);
        if (old_w != NULL)
            old_w->focused = false;
    }

    else if (is_rename) {
        struct workspace *w = workspace_lookup(m, current_id);
        assert(w != NULL);

        struct json_object *_current_name;
        if (!json_object_object_get_ex(current, "name", &_current_name)) {
            LOG_ERR("workspace 'rename' event without 'name' property");
            mtx_unlock(&mod->lock);
            return false;
        }

        free(w->name);
        w->name = strdup(json_object_get_string(_current_name));
        w->name_as_int = workspace_name_as_int(w->name);

        /* Re-add the workspace to ensure correct sorting */
        struct workspace ws = *w;
        tll_foreach(m->workspaces, it) {
            if (it->item.id == current_id) {
                tll_remove(m->workspaces, it);
                break;
            }
        }
        workspace_add(m, ws);
    }

    else if (is_move) {
        struct workspace *w = workspace_lookup(m, current_id);
        assert(w != NULL);

        struct json_object *_current_output;
        if (!json_object_object_get_ex(current, "output", &_current_output)) {
            LOG_ERR("workspace 'move' event without 'output' property");
            mtx_unlock(&mod->lock);
            return false;
        }

        free(w->output);
        w->output = strdup(json_object_get_string(_current_output));

        /*
         * If the moved workspace was focused, schedule a full update because
         * visibility for other workspaces may have changed.
         */
        if (w->focused) {
            i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);
        }
    }

    else if (is_urgent) {
        struct json_object *urgent;
        if (!json_object_object_get_ex(current, "urgent", &urgent)) {
            LOG_ERR("workspace 'urgent' event without 'urgent' property");
            mtx_unlock(&mod->lock);
            return false;
        }

        struct workspace *w = workspace_lookup(m, current_id);
        w->urgent = json_object_get_boolean(urgent);
    }

    else {
        LOG_WARN("unimplemented workspace event '%s'", change_str);
    }

    m->dirty = true;
    mtx_unlock(&mod->lock);
    return true;

err:
    mtx_unlock(&mod->lock);
    return false;
}

static bool
handle_window_event(int sock, int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change)) {
        LOG_ERR("window event without 'change' property");
        return false;
    }

    const char *change_str = json_object_get_string(change);
    bool is_new = strcmp(change_str, "new") == 0;
    bool is_focus = strcmp(change_str, "focus") == 0;
    bool is_close = strcmp(change_str, "close") == 0;
    bool is_title = strcmp(change_str, "title") == 0;

    if (!is_new && !is_focus && !is_close && !is_title)
        return true;

    mtx_lock(&mod->lock);

    struct workspace *ws = NULL;
    size_t focused = 0;
    tll_foreach(m->workspaces, it) {
        if (it->item.focused) {
            ws = &it->item;
            focused++;
        }
    }

    assert(focused == 1);
    assert(ws != NULL);

    if (is_close) {
        free(ws->window.title);
        free(ws->window.application);

        ws->window.id = -1;
        ws->window.title = ws->window.application = NULL;
        ws->window.pid = -1;

        /* May not be true, but e.g. a subsequent “focus” event will
         * reset it... */
        ws->empty = true;

        m->dirty = true;
        mtx_unlock(&mod->lock);
        return true;

    }

    /* Non-close event - thus workspace cannot be empty */
    ws->empty = false;

    struct json_object *container, *id, *name;
    if (!json_object_object_get_ex(json, "container", &container) ||
        !json_object_object_get_ex(container, "id", &id) ||
        !json_object_object_get_ex(container, "name", &name))
    {
        mtx_unlock(&mod->lock);
        LOG_ERR("window event without 'container' with 'id' and 'name'");
        return false;
    }

    if (is_title && ws->window.id != json_object_get_int(id)) {
        /* Ignore title changed event if it's not current window */
        mtx_unlock(&mod->lock);
        return true;
    }

    free(ws->window.title);

    const char *title = json_object_get_string(name);
    ws->window.title = title != NULL ? strdup(title) : NULL;
    ws->window.id = json_object_get_int(id);

    /*
     * Sway only!
     *
     * Use 'app_id' for 'application' tag, if it exists.
     *
     * Otherwise, use 'pid' if it exists, and read application name
     * from /proc/zpid>/comm
     */

    struct json_object *app_id;
    struct json_object *pid;

    if (json_object_object_get_ex(container, "app_id", &app_id) &&
        json_object_get_string(app_id) != NULL)
    {
        free(ws->window.application);
        ws->window.application = strdup(json_object_get_string(app_id));
        LOG_DBG("application: \"%s\", via 'app_id'", ws->window.application);
    }

    /* If PID has changed, update application name from /proc/<pid>/comm */
    else if (json_object_object_get_ex(container, "pid", &pid) &&
             ws->window.pid != json_object_get_int(pid))
    {
        ws->window.pid = json_object_get_int(pid);

        char path[64];
        snprintf(path, sizeof(path), "/proc/%u/comm", ws->window.pid);

        int fd = open(path, O_RDONLY);
        if (fd == -1) {
            /* Application may simply have terminated */
            free(ws->window.application); ws->window.application = NULL;
            ws->window.pid = -1;

            m->dirty = true;
            mtx_unlock(&mod->lock);
            return true;
        }

        char application[128];
        ssize_t bytes = read(fd, application, sizeof(application));
        assert(bytes >= 0);

        application[bytes - 1] = '\0';
        free(ws->window.application);
        ws->window.application = strdup(application);
        close(fd);
        LOG_DBG("application: \"%s\", via 'pid'", ws->window.application);
    }

    m->dirty = true;
    mtx_unlock(&mod->lock);
    return true;
}

static bool
handle_mode_event(int sock, int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change)) {
        LOG_ERR("mode event without 'change' property");
        return false;
    }

    const char *current_mode = json_object_get_string(change);

    mtx_lock(&mod->lock);
    {
        free(m->mode);
        m->mode = strdup(current_mode);
        m->dirty = true;
    }
    mtx_unlock(&mod->lock);
    return true;
}

static void
burst_done(void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    if (m->dirty) {
        m->dirty = false;
        mod->bar->refresh(mod->bar);
    }
}

static int
run(struct module *mod)
{
    struct sockaddr_un addr;
    if (!i3_get_socket_address(&addr))
        return 1;

    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock == -1) {
        LOG_ERRNO("failed to create UNIX socket");
        return 1;
    }

    int r = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    if (r == -1) {
        LOG_ERRNO("failed to connect to i3 socket");
        close(sock);
        return 1;
    }

    struct private *m = mod->private;
    for (size_t i = 0; i < m->persistent_count; i++) {
        const char *name_as_string = m->persistent_workspaces[i];

        int name_as_int = workspace_name_as_int(name_as_string);
        if (m->strip_workspace_numbers) {
            const char *colon = strchr(name_as_string, ':');
            if (colon != NULL)
                name_as_string = colon++;
        }

        struct workspace ws = {
            .id = -1,
            .name = strdup(name_as_string),
            .name_as_int = name_as_int,
            .persistent = true,
            .empty = true,
        };
        workspace_add(m, ws);
    }

    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_VERSION, NULL);
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\", \"window\", \"mode\"]");
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);

    static const struct i3_ipc_callbacks callbacks = {
        .burst_done = &burst_done,
        .reply_version = &handle_get_version_reply,
        .reply_subscribe = &handle_subscribe_reply,
        .reply_workspaces = &handle_get_workspaces_reply,
        .event_workspace = &handle_workspace_event,
        .event_window = &handle_window_event,
        .event_mode = &handle_mode_event,
    };

    bool ret = i3_receive_loop(mod->abort_fd, sock, &callbacks, mod);
    close(sock);
    return ret ? 0 : 1;
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

    for (size_t i = 0; i < m->ws_content.count; i++) {
        struct particle *p = m->ws_content.v[i].content;
        p->destroy(p);
        free(m->ws_content.v[i].name);
    }

    free(m->ws_content.v);
    workspaces_free(m, true);

    for (size_t i = 0; i < m->persistent_count; i++)
        free(m->persistent_workspaces[i]);
    free(m->persistent_workspaces);

    free(m->mode);
    free(m);
    module_default_destroy(mod);
}

static struct ws_content *
ws_content_for_name(struct private *m, const char *name)
{
    for (size_t i = 0; i < m->ws_content.count; i++) {
        struct ws_content *content = &m->ws_content.v[i];
        if (strcmp(content->name, name) == 0)
            return content;
    }

    return NULL;
}

static const char *
description(const struct module *mod)
{
    return "i3/sway";
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    size_t particle_count = 0;
    struct exposable *particles[tll_length(m->workspaces) + 1];
    struct exposable *current = NULL;

    tll_foreach(m->workspaces, it) {
        struct workspace *ws = &it->item;
        const struct ws_content *template = NULL;

        /* Lookup content template for workspace. Fall back to default
         * template if this workspace doesn't have a specific
         * template */
        if (ws->name == NULL) {
            LOG_ERR("%d %d", ws->name_as_int, ws->id);
        }
        template = ws_content_for_name(m, ws->name);
        if (template == NULL) {
            LOG_DBG("no ws template for %s, using default template", ws->name);
            template = ws_content_for_name(m, "");
        }

        const char *state =
            ws->urgent ? "urgent" :
            ws->visible ? ws->focused ? "focused" : "unfocused" :
            "invisible";

        LOG_DBG("name=%s (name-as-int=%d): visible=%s, focused=%s, urgent=%s, empty=%s, state=%s, "
                "application=%s, title=%s, mode=%s",
                ws->name, ws->name_as_int,
                ws->visible ? "yes" : "no",
                ws->focused ? "yes" : "no",
                ws->urgent ? "yes" : "no",
                ws->empty ? "yes" : "no",
                state,
                ws->window.application,
                ws->window.title,
                m->mode);

        const char *name = ws->name;

        if (m->strip_workspace_numbers) {
            const char *colon = strchr(name, ':');
            if (colon != NULL)
                name = colon + 1;
        }

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "name", name),
                tag_new_bool(mod, "visible", ws->visible),
                tag_new_bool(mod, "focused", ws->focused),
                tag_new_bool(mod, "urgent", ws->urgent),
                tag_new_bool(mod, "empty", ws->empty),
                tag_new_string(mod, "state", state),

                tag_new_string(mod, "application", ws->window.application),
                tag_new_string(mod, "title", ws->window.title),

                tag_new_string(mod, "mode", m->mode),
            },
            .count = 9,
        };

        if (ws->focused) {
            const struct ws_content *cur = ws_content_for_name(m, "current");
            if (cur != NULL)
                current = cur->content->instantiate(cur->content, &tags);
        }

        if (template == NULL) {
            LOG_WARN(
                "no ws template for %s, and no default template available",
                ws->name);
        } else {
            particles[particle_count++] = template->content->instantiate(
                template->content, &tags);
        }

        tag_set_destroy(&tags);
    }

    if (current != NULL)
        particles[particle_count++] = current;

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(
        particles, particle_count, m->left_spacing, m->right_spacing);
}

/* Maps workspace name to a content particle. */
struct i3_workspaces {
    const char *name;
    struct particle *content;
};

static struct module *
i3_new(struct i3_workspaces workspaces[], size_t workspace_count,
       int left_spacing, int right_spacing, enum sort_mode sort_mode,
       size_t persistent_count,
       const char *persistent_workspaces[static persistent_count],
       bool strip_workspace_numbers)
{
    struct private *m = calloc(1, sizeof(*m));

    m->mode = strdup("default");
    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    m->ws_content.count = workspace_count;
    m->ws_content.v = malloc(workspace_count * sizeof(m->ws_content.v[0]));

    for (size_t i = 0; i < workspace_count; i++) {
        m->ws_content.v[i].name = strdup(workspaces[i].name);
        m->ws_content.v[i].content = workspaces[i].content;
    }

    m->strip_workspace_numbers = strip_workspace_numbers;
    m->sort_mode = sort_mode;

    m->persistent_count = persistent_count;
    m->persistent_workspaces = calloc(
        persistent_count, sizeof(m->persistent_workspaces[0]));

    for (size_t i = 0; i < persistent_count; i++)
        m->persistent_workspaces[i] = strdup(persistent_workspaces[i]);

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
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing = yml_get_value(node, "left-spacing");
    const struct yml_node *right_spacing = yml_get_value(node, "right-spacing");
    const struct yml_node *sort = yml_get_value(node, "sort");
    const struct yml_node *persistent = yml_get_value(node, "persistent");
    const struct yml_node *strip_workspace_number = yml_get_value(
        node, "strip-workspace-numbers");

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    const char *sort_value = sort != NULL ? yml_value_as_string(sort) : NULL;
    enum sort_mode sort_mode =
        sort_value == NULL ? SORT_NONE :
        strcmp(sort_value, "none") == 0 ? SORT_NONE :
        strcmp(sort_value, "native") == 0 ? SORT_NATIVE :
        strcmp(sort_value, "ascending") == 0 ? SORT_ASCENDING : SORT_DESCENDING;

    const size_t persistent_count =
        persistent != NULL ? yml_list_length(persistent) : 0;
    const char *persistent_workspaces[persistent_count];

    if (persistent != NULL) {
        size_t idx = 0;
        for (struct yml_list_iter it = yml_list_iter(persistent);
             it.node != NULL;
             yml_list_next(&it), idx++)
        {
            persistent_workspaces[idx] = yml_value_as_string(it.node);
        }
    }

    struct i3_workspaces workspaces[yml_dict_length(c)];

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(c);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        workspaces[idx].name = yml_value_as_string(it.key);
        workspaces[idx].content = conf_to_particle(it.value, inherited);
    }

    return i3_new(workspaces, yml_dict_length(c), left, right, sort_mode,
                  persistent_count, persistent_workspaces,
                  (strip_workspace_number != NULL
                   ? yml_value_as_bool(strip_workspace_number) : false));
}

static bool
verify_content(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            conf_err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string (a i3 workspace name)",
                    conf_err_prefix(chain, it.key));
            return false;
        }

        if (!conf_verify_particle(chain_push(chain, key), it.value))
            return false;

        chain_pop(chain);
    }

    return true;
}

static bool
verify_sort(keychain_t *chain, const struct yml_node *node)
{
    return conf_verify_enum(
        chain, node, (const char *[]){"none", "native", "ascending", "descending"}, 4);
}

static bool
verify_persistent(keychain_t *chain, const struct yml_node *node)
{
    return conf_verify_list(chain, node, &conf_verify_string);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"spacing", false, &conf_verify_unsigned},
        {"left-spacing", false, &conf_verify_unsigned},
        {"right-spacing", false, &conf_verify_unsigned},
        {"sort", false, &verify_sort},
        {"persistent", false, &verify_persistent},
        {"strip-workspace-numbers", false, &conf_verify_bool},
        {"content", true, &verify_content},
        {"anchors", false, NULL},
        {NULL, false, NULL},
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_i3_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_i3_iface"))) ;
#endif
