#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>

#include <poll.h>

#include <sys/socket.h>
#include <sys/un.h>

#if defined(ENABLE_X11)
 #include <xcb/xcb.h>
 #include <xcb/xcb_aux.h>
#endif

#include <i3/ipc.h>

#include <json-c/json_tokener.h>
#include <json-c/json_util.h>
#include <json-c/linkhash.h>

#define LOG_MODULE "i3"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

#if defined(ENABLE_X11)
 #include "../xcb.h"
#endif

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
};

struct private {
    int left_spacing;
    int right_spacing;

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
    assert(json_object_is_type(json, json_type_object));
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'workspace' object is not of type 'object'");
        return false;
    }

    struct json_object *name = json_object_object_get(json, "name");
    struct json_object *output = json_object_object_get(json, "output");
    const struct json_object *visible = json_object_object_get(json, "visible");
    const struct json_object *focused = json_object_object_get(json, "focused");
    const struct json_object *urgent = json_object_object_get(json, "urgent");

    if (name == NULL || !json_object_is_type(name, json_type_string)) {
        LOG_ERR("'workspace' object has no 'name' string value");
        return false;
    }

    if (output == NULL || !json_object_is_type(output, json_type_string)) {
        LOG_ERR("'workspace' object has no 'output' string value");
        return false;
    }

    if (visible != NULL && !json_object_is_type(visible, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'visible' value is not a boolean");
        return false;
    }

    if (focused != NULL && !json_object_is_type(focused, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'focused' value is not a boolean");
        return false;
    }

    if (urgent != NULL && !json_object_is_type(urgent, json_type_boolean)) {
        LOG_ERR("'workspace' object's 'urgent' value is not a boolean");
        return false;
    }

    *ws = (struct workspace) {
        .name = strdup(json_object_get_string(name)),
        .output = strdup(json_object_get_string(output)),
        .visible = json_object_get_boolean(visible),
        .focused = json_object_get_boolean(focused),
        .urgent = json_object_get_boolean(urgent),
    };

    return true;
}

static void
workspace_free(struct workspace ws)
{
    free(ws.name);
    free(ws.output);
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
send_pkg(int sock, int cmd, char *data)
{
    size_t size = data != NULL ? strlen(data) : 0;
    i3_ipc_header_t hdr = {
        .magic = I3_IPC_MAGIC,
        .size = size,
        .type = cmd
    };

    if (write(sock, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
        return false;

    if (data != NULL) {
        if (write(sock, data, size) != (ssize_t)size)
            return false;
    }

    return true;
}

static bool
handle_get_version_reply(struct private *m, const struct json_object *json)
{
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'version' reply is not of type 'object'");
        return false;
    }

    struct json_object *version = json_object_object_get(json, "human_readable");

    if (version == NULL || !json_object_is_type(version, json_type_string)) {
        LOG_ERR("'version' reply did not contain a 'human_readable' string value");
        return false;
    }

    LOG_INFO("i3: %s", json_object_get_string(version));
    return true;
}

static bool
handle_subscribe_reply(struct private *m, const struct json_object *json)
{
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'subscribe' reply is not of type 'object'");
        return false;
    }

    const struct json_object *success = json_object_object_get(json, "success");

    if (success == NULL || !json_object_is_type(success, json_type_boolean)) {
        LOG_ERR("'subscribe' reply did not contain a 'success' boolean value");
        return false;
    }

    if (!json_object_get_boolean(success)) {
        LOG_ERR("failed to subscribe");
        return false;
    }

    return true;
}

static bool
handle_get_workspaces_reply(struct private *m, const struct json_object *json)
{
    if (!json_object_is_type(json, json_type_array)) {
        LOG_ERR("'workspaces' reply is not of type 'array'");
        return false;
    }

    workspaces_free(m);

    size_t count = json_object_array_length(json);
    m->workspaces.count = count;
    m->workspaces.v = malloc(count * sizeof(m->workspaces.v[0]));

    for (size_t i = 0; i < count; i++) {
        if (!workspace_from_json(
                json_object_array_get_idx(json, i), &m->workspaces.v[i])) {
            workspaces_free(m);
            return false;
        }

        LOG_DBG("#%zu: %s", i, m->workspaces.v[i].name);
    }

    return true;
}

static bool
handle_workspace_event(struct private *m, const struct json_object *json)
{
    if (!json_object_is_type(json, json_type_object)) {
        LOG_ERR("'workspace' event is not of type 'object'");
        return false;
    }

    struct json_object *change = json_object_object_get(json, "change");
    const struct json_object *current = json_object_object_get(json, "current");
    const struct json_object *old = json_object_object_get(json, "old");
    struct json_object *current_name = NULL;

    if (change == NULL || !json_object_is_type(change, json_type_string)) {
        LOG_ERR("'workspace' event did not contain a 'change' string value");
        return false;
    }

    const char *change_str = json_object_get_string(change);
    bool is_urgent = strcmp(change_str, "reload") == 0;

    if (!is_urgent) {
        if (current == NULL || !json_object_is_type(current, json_type_object)) {
            LOG_ERR("'workspace' event did not contain a 'current' object value");
            return false;
        }

        current_name = json_object_object_get(current, "name");
        if (current_name == NULL ||
            !json_object_is_type(current_name, json_type_string))
        {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'name' string value");
            return false;
        }
    }

    if (strcmp(change_str, "init") == 0) {
        assert(workspace_lookup(m, json_object_get_string(current_name)) == NULL);

        struct workspace ws;
        if (!workspace_from_json(current, &ws))
            return false;

        workspace_add(m, ws);
    }

    else if (strcmp(change_str, "empty") == 0) {
        assert(workspace_lookup(m, json_object_get_string(current_name)) != NULL);
        workspace_del(m, json_object_get_string(current_name));
    }

    else if (strcmp(change_str, "focus") == 0) {
        if (old == NULL || !json_object_is_type(old, json_type_object)) {
            LOG_ERR("'workspace' event did not contain a 'old' object value");
            return false;
        }

        struct json_object *old_name = json_object_object_get(old, "name");
        if (old_name == NULL || !json_object_is_type(old_name, json_type_string)) {
            LOG_ERR("'workspace' event's 'old' object did not "
                    "contain a 'name' string value");
            return false;
        }

        struct workspace *w = workspace_lookup(
            m, json_object_get_string(current_name));
        LOG_DBG("w: %s", w->name);
        assert(w != NULL);

        /* Mark all workspaces on current's output invisible */
        for (size_t i = 0; i < m->workspaces.count; i++) {
            struct workspace *ws = &m->workspaces.v[i];
            if (strcmp(ws->output, w->output) == 0)
                ws->visible = false;
        }

        const struct json_object *urgent = json_object_object_get(current, "urgent");
        if (urgent == NULL || !json_object_is_type(urgent, json_type_boolean)) {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'urgent' boolean value");
            return false;
        }

        w->urgent = json_object_get_boolean(urgent);
        w->focused = true;
        w->visible = true;

        /* Old workspace is no longer focused */
        struct workspace *old_w = workspace_lookup(
            m, json_object_get_string(old_name));
        if (old_w != NULL)
            old_w->focused = false;
    }

    else if (strcmp(change_str, "urgent") == 0){
        const struct json_object *urgent = json_object_object_get(current, "urgent");
        if (urgent == NULL || !json_object_is_type(urgent, json_type_boolean)) {
            LOG_ERR("'workspace' event's 'current' object did not "
                    "contain a 'urgent' boolean value");
            return false;
        }

        struct workspace *w = workspace_lookup(
            m, json_object_get_string(current_name));
        w->urgent = json_object_get_boolean(urgent);
    }

    else if (strcmp(change_str, "reload") == 0)
        LOG_WARN("unimplemented: 'reload' event");

    return true;
}

#if defined(ENABLE_X11)
static bool
get_socket_address_x11(struct sockaddr_un *addr)
{
    int default_screen;
    xcb_connection_t *conn = xcb_connect(NULL, &default_screen);
    if (xcb_connection_has_error(conn) > 0) {
        LOG_ERR("failed to connect to X");
        xcb_disconnect(conn);
        return false;
    }

    xcb_screen_t *screen = xcb_aux_get_screen(conn, default_screen);

    xcb_atom_t atom = get_atom(conn, "I3_SOCKET_PATH");
    assert(atom != XCB_ATOM_NONE);

    xcb_get_property_cookie_t cookie
        = xcb_get_property_unchecked(
            conn, false, screen->root, atom,
            XCB_GET_PROPERTY_TYPE_ANY, 0, sizeof(addr->sun_path));

    xcb_generic_error_t *err;
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(conn, cookie, &err);

    if (err != NULL) {
        LOG_ERR("failed to get i3 socket path: %s", xcb_error(err));
        free(err);
        free(reply);
        return false;
    }

    const int len = xcb_get_property_value_length(reply);
    assert(len < sizeof(addr->sun_path));

    if (len == 0) {
        LOG_ERR("failed to get i3 socket path: empty reply");
        free(reply);
        return false;
    }

    memcpy(addr->sun_path, xcb_get_property_value(reply), len);
    addr->sun_path[len] = '\0';

    free(reply);
    xcb_disconnect(conn);
    return true;
}
#endif

static bool
get_socket_address(struct sockaddr_un *addr)
{
    *addr = (struct sockaddr_un){.sun_family = AF_UNIX};

    const char *sway_sock = getenv("SWAYSOCK");
    if (sway_sock == NULL) {
#if defined(ENABLE_X11)
        return get_socket_address_x11(addr);
#else
        return false;
#endif
    }

    strncpy(addr->sun_path, sway_sock, sizeof(addr->sun_path) - 1);
    return true;
}

static int
run(struct module *mod)
{
    struct private *m = mod->private;

    struct sockaddr_un addr;
    if (!get_socket_address(&addr))
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

    send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_VERSION, NULL);
    send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);
    send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\"]");

    /* Initial reply typically requires a couple of KB. But we often
     * need more later. For example, switching workspaces can result
     * in quite big notification messages. */
    size_t reply_buf_size = 4096;
    char *buf = malloc(reply_buf_size);
    size_t buf_idx = 0;

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = sock, .events = POLLIN}
        };

        int res = poll(fds, 2, -1);
        if (res <= 0) {
            LOG_ERRNO("failed to poll()");
            break;
        }

        if (fds[0].revents & POLLIN)
            break;

        assert(fds[1].revents & POLLIN);

        /* Grow receive buffer, if necessary */
        if (buf_idx == reply_buf_size) {
            LOG_DBG("growing reply buffer: %zu -> %zu",
                    reply_buf_size, reply_buf_size * 2);

            char *new_buf = realloc(buf, reply_buf_size * 2);
            if (new_buf == NULL) {
                LOG_ERR("failed to grow reply buffer from %zu to %zu bytes",
                        reply_buf_size, reply_buf_size * 2);
                break;
            }

            buf = new_buf;
            reply_buf_size *= 2;
        }

        assert(reply_buf_size > buf_idx);

        ssize_t bytes = read(sock, &buf[buf_idx], reply_buf_size - buf_idx);
        if (bytes < 0) {
            LOG_ERRNO("failed to read from i3's socket");
            break;
        }

        buf_idx += bytes;

        bool err = false;
        bool need_bar_refresh = false;

        while (!err && buf_idx >= sizeof(i3_ipc_header_t)) {
            const i3_ipc_header_t *hdr = (const i3_ipc_header_t *)buf;
            if (strncmp(hdr->magic, I3_IPC_MAGIC, sizeof(hdr->magic)) != 0) {
                LOG_ERR(
                    "i3 IPC header magic mismatch: expected \"%.*s\", got \"%.*s\"",
                    (int)sizeof(hdr->magic), I3_IPC_MAGIC,
                    (int)sizeof(hdr->magic), hdr->magic);

                err = true;
                break;
            }

            size_t total_size = sizeof(i3_ipc_header_t) + hdr->size;

            if (total_size > buf_idx) {
                LOG_DBG("got %zd bytes, need %zu", bytes, total_size);
                break;
            }

            /* Json-c expects a NULL-terminated string */
            char json_str[hdr->size + 1];
            memcpy(json_str, &buf[sizeof(*hdr)], hdr->size);
            json_str[hdr->size] = '\0';
            //printf("raw: %s\n", json_str);
            LOG_DBG("raw: %s\n", json_str);

            json_tokener *tokener = json_tokener_new();
            struct json_object *json = json_tokener_parse(json_str);
            if (json == NULL) {
                LOG_ERR("failed to parse json");
                err = true;
                break;
            }

            mtx_lock(&mod->lock);

            switch (hdr->type) {
            case I3_IPC_REPLY_TYPE_VERSION:
                handle_get_version_reply(m, json);
                break;

            case I3_IPC_REPLY_TYPE_SUBSCRIBE:
                handle_subscribe_reply(m, json);
                break;

            case I3_IPC_REPLY_TYPE_WORKSPACES:
                handle_get_workspaces_reply(m, json);
                need_bar_refresh = true;
                break;

            case I3_IPC_EVENT_WORKSPACE:
                handle_workspace_event(m, json);
                need_bar_refresh = true;
                break;

            case I3_IPC_EVENT_OUTPUT:
            case I3_IPC_EVENT_MODE:
            case I3_IPC_EVENT_WINDOW:
            case I3_IPC_EVENT_BARCONFIG_UPDATE:
            case I3_IPC_EVENT_BINDING:
            case I3_IPC_EVENT_SHUTDOWN:
            case I3_IPC_EVENT_TICK:
                break;

            default: assert(false);
            }

            mtx_unlock(&mod->lock);

            json_object_put(json);
            json_tokener_free(tokener);

            assert(total_size <= buf_idx);
            memmove(buf, &buf[total_size], buf_idx - total_size);
            buf_idx -= total_size;
        }

        if (err)
            break;

        if (need_bar_refresh)
            mod->bar->refresh(mod->bar);
    }

    free(buf);
    close(sock);
    return 0;
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

    struct exposable *particles[m->workspaces.count];

    size_t particle_count = 0;
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

        if (template == NULL) {
            LOG_WARN("no ws template for %s, and no default template available", ws->name);
            continue;
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
            },
            .count = 5,
        };

        particles[particle_count++] = template->content->instantiate(
            template->content, &tags);

        tag_set_destroy(&tags);
    }

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
    struct private *m = malloc(sizeof(*m));

    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    m->ws_content.count = workspace_count;
    m->ws_content.v = malloc(workspace_count * sizeof(m->ws_content.v[0]));

    for (size_t i = 0; i < workspace_count; i++) {
        m->ws_content.v[i].name = strdup(workspaces[i].name);
        m->ws_content.v[i].content = workspaces[i].content;
    }

    m->workspaces.v = NULL;
    m->workspaces.count = 0;

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
        MODULE_COMMON_ATTRS,
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
