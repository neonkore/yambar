#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>

#include <sys/types.h>
#include <fcntl.h>

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

struct ws_content {
    char *name;
    struct particle *content;
};

struct workspace {
    char *name;
    char *output;
    bool visible;
    bool focused;
    bool urgent;

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

    struct {
        struct ws_content *v;
        size_t count;
    } ws_content;

    struct {
        struct workspace *v;
        size_t count;
    } workspaces;
};

static bool
workspace_from_json(const struct json_object *json, struct workspace *ws)
{
    /* Always present */
    struct json_object *name, *output;
    if (!json_object_object_get_ex(json, "name", &name) ||
        !json_object_object_get_ex(json, "output", &output))
    {
        return false;
    }

    /* Optional */
    struct json_object *visible = NULL, *focused = NULL, *urgent = NULL;
    json_object_object_get_ex(json, "visible", &visible);
    json_object_object_get_ex(json, "focused", &focused);
    json_object_object_get_ex(json, "urgent", &urgent);

    *ws = (struct workspace) {
        .name = strdup(json_object_get_string(name)),
        .output = strdup(json_object_get_string(output)),
        .visible = json_object_get_boolean(visible),
        .focused = json_object_get_boolean(focused),
        .urgent = json_object_get_boolean(urgent),
        .window = {.title = NULL, .pid = -1},
    };

    return true;
}

static void
workspace_free(struct workspace ws)
{
    free(ws.name);
    free(ws.output);
    free(ws.window.title);
    free(ws.window.application);
}

static void
workspaces_free(struct private *m)
{
    for (size_t i = 0; i < m->workspaces.count; i++)
        workspace_free(m->workspaces.v[i]);

    free(m->workspaces.v);
    m->workspaces.v = NULL;
    m->workspaces.count = 0;
}

static void
workspace_add(struct private *m, struct workspace ws)
{
    size_t new_count = m->workspaces.count + 1;
    struct workspace *new_v = realloc(m->workspaces.v, new_count * sizeof(new_v[0]));

    m->workspaces.count = new_count;
    m->workspaces.v = new_v;
    m->workspaces.v[new_count - 1] = ws;
}

static void
workspace_del(struct private *m, const char *name)
{
    struct workspace *workspaces = m->workspaces.v;

    for (size_t i = 0; i < m->workspaces.count; i++) {
        const struct workspace *ws = &workspaces[i];

        if (strcmp(ws->name, name) != 0)
            continue;

        workspace_free(*ws);

        memmove(
            &workspaces[i],
            &workspaces[i + 1],
            (m->workspaces.count - i - 1) * sizeof(workspaces[0]));
        m->workspaces.count--;
        break;
    }
}

static struct workspace *
workspace_lookup(struct private *m, const char *name)
{
    for (size_t i = 0; i < m->workspaces.count; i++)
        if (strcmp(m->workspaces.v[i].name, name) == 0)
            return &m->workspaces.v[i];
    return NULL;
}


static bool
handle_get_version_reply(int type, const struct json_object *json, void *_m)
{
    struct json_object *version;
    if (!json_object_object_get_ex(json, "human_readable", &version))
        return false;

    LOG_INFO("i3: %s", json_object_get_string(version));
    return true;
}

static bool
handle_subscribe_reply(int type, const struct json_object *json, void *_m)
{
    struct json_object *success;
    if (!json_object_object_get_ex(json, "success", &success))
        return false;

    if (!json_object_get_boolean(success)) {
        LOG_ERR("failed to subscribe");
        return false;
    }

    return true;
}

static bool
handle_get_workspaces_reply(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    workspaces_free(m);
    m->dirty = true;

    size_t count = json_object_array_length(json);
    m->workspaces.count = count;
    m->workspaces.v = calloc(count, sizeof(m->workspaces.v[0]));

    for (size_t i = 0; i < count; i++) {
        if (!workspace_from_json(
                json_object_array_get_idx(json, i), &m->workspaces.v[i])) {
            workspaces_free(m);
            mtx_unlock(&mod->lock);
            return false;
        }

        LOG_DBG("#%zu: %s", i, m->workspaces.v[i].name);
    }

    mtx_unlock(&mod->lock);
    return true;
}

static bool
handle_workspace_event(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change))
        return false;

    const char *change_str = json_object_get_string(change);

    bool is_init = strcmp(change_str, "init") == 0;
    bool is_empty = strcmp(change_str, "empty") == 0;
    bool is_focused = strcmp(change_str, "focus") == 0;
    bool is_urgent = strcmp(change_str, "urgent") == 0;
    bool is_reload = strcmp(change_str, "reload") == 0;

    if (is_reload) {
        LOG_WARN("unimplemented: 'reload' event");
        return true;
    }

    struct json_object *current, *_current_name;
    if (!json_object_object_get_ex(json, "current", &current) ||
        !json_object_object_get_ex(current, "name", &_current_name))
        return false;

    const char *current_name = json_object_get_string(_current_name);

    mtx_lock(&mod->lock);

    if (is_init) {
        assert(workspace_lookup(m, current_name) == NULL);

        struct workspace ws;
        if (!workspace_from_json(current, &ws))
            goto err;

        workspace_add(m, ws);
    }

    else if (is_empty) {
        assert(workspace_lookup(m, current_name) != NULL);
        workspace_del(m, current_name);
    }

    else if (is_focused) {
        struct json_object *old, *_old_name, *urgent;
        if (!json_object_object_get_ex(json, "old", &old) ||
            !json_object_object_get_ex(old, "name", &_old_name) ||
            !json_object_object_get_ex(current, "urgent", &urgent))
        {
            mtx_unlock(&mod->lock);
            return false;
        }

        struct workspace *w = workspace_lookup(m, current_name);
        assert(w != NULL);

        LOG_DBG("w: %s", w->name);

        /* Mark all workspaces on current's output invisible */
        for (size_t i = 0; i < m->workspaces.count; i++) {
            struct workspace *ws = &m->workspaces.v[i];
            if (strcmp(ws->output, w->output) == 0)
                ws->visible = false;
        }

        w->urgent = json_object_get_boolean(urgent);
        w->focused = true;
        w->visible = true;

        /* Old workspace is no longer focused */
        const char *old_name = json_object_get_string(_old_name);
        struct workspace *old_w = workspace_lookup(m, old_name);
        if (old_w != NULL)
            old_w->focused = false;
    }

    else if (is_urgent) {
        struct json_object *urgent;
        if (!json_object_object_get_ex(current, "urgent", &urgent)) {
            mtx_unlock(&mod->lock);
            return false;
        }

        struct workspace *w = workspace_lookup(m, current_name);
        w->urgent = json_object_get_boolean(urgent);
    }

    m->dirty = true;
    mtx_unlock(&mod->lock);
    return true;

err:
    mtx_unlock(&mod->lock);
    return false;
}

static bool
handle_window_event(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change))
        return false;

    const char *change_str = json_object_get_string(change);
    bool is_focus = strcmp(change_str, "focus") == 0;
    bool is_close = strcmp(change_str, "close") == 0;
    bool is_title = strcmp(change_str, "title") == 0;

    if (!is_focus && !is_close && !is_title)
        return true;

    mtx_lock(&mod->lock);

    struct workspace *ws = NULL;
    size_t focused = 0;
    for (size_t i = 0; i < m->workspaces.count; i++) {
        if (m->workspaces.v[i].focused) {
            ws = &m->workspaces.v[i];
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

        m->dirty = true;
        mtx_unlock(&mod->lock);
        return true;

    }

    struct json_object *container, *id, *name, *pid;
    if (!json_object_object_get_ex(json, "container", &container) ||
        !json_object_object_get_ex(container, "id", &id) ||
        !json_object_object_get_ex(container, "name", &name) ||
        !json_object_object_get_ex(container, "pid", &pid))
    {
        mtx_unlock(&mod->lock);
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

    /* If PID has changed, update application name from /proc/<pid>/comm */
    if (ws->window.pid != json_object_get_int(pid)) {
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
    }

    m->dirty = true;
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

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
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

    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_VERSION, NULL);
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\", \"window\"]");
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);

    static const struct i3_ipc_callbacks callbacks = {
        .burst_done = &burst_done,
        .reply_version = &handle_get_version_reply,
        .reply_subscribe = &handle_subscribe_reply,
        .reply_workspaces = &handle_get_workspaces_reply,
        .event_workspace = &handle_workspace_event,
        .event_window = &handle_window_event,
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
    workspaces_free(m);

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

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    size_t particle_count = 0;
    struct exposable *particles[m->workspaces.count + 1];
    struct exposable *current = NULL;

    for (size_t i = 0; i < m->workspaces.count; i++) {
        const struct workspace *ws = &m->workspaces.v[i];
        const struct ws_content *template = NULL;

        /* Lookup content template for workspace. Fall back to default
         * template if this workspace doesn't have a specific
         * template */
        template = ws_content_for_name(m, ws->name);
        if (template == NULL) {
            LOG_DBG("no ws template for %s, using default template", ws->name);
            template = ws_content_for_name(m, "");
        }

        const char *state =
            ws->urgent ? "urgent" :
            ws->visible ? ws->focused ? "focused" : "unfocused" :
            "invisible";

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "name", ws->name),
                tag_new_bool(mod, "visible", ws->visible),
                tag_new_bool(mod, "focused", ws->focused),
                tag_new_bool(mod, "urgent", ws->urgent),
                tag_new_string(mod, "state", state),

                tag_new_string(mod, "application", ws->window.application),
                tag_new_string(mod, "title", ws->window.title),
            },
            .count = 7,
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
       int left_spacing, int right_spacing)
{
    struct private *m = calloc(1, sizeof(*m));

    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    m->ws_content.count = workspace_count;
    m->ws_content.v = malloc(workspace_count * sizeof(m->ws_content.v[0]));

    for (size_t i = 0; i < workspace_count; i++) {
        m->ws_content.v[i].name = strdup(workspaces[i].name);
        m->ws_content.v[i].content = workspaces[i].content;
    }

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing = yml_get_value(node, "left-spacing");
    const struct yml_node *right_spacing = yml_get_value(node, "right-spacing");

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    struct i3_workspaces workspaces[yml_dict_length(c)];

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(c);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        workspaces[idx].name = yml_value_as_string(it.key);
        workspaces[idx].content = conf_to_particle(it.value, inherited);
    }

    return i3_new(workspaces, yml_dict_length(c), left, right);
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
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"spacing", false, &conf_verify_int},
        {"left-spacing", false, &conf_verify_int},
        {"right-spacing", false, &conf_verify_int},
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
