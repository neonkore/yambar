#include "i3.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>

#include <poll.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <i3/ipc.h>
#include <json-c/json_tokener.h>
#include <json-c/json_util.h>
#include <json-c/linkhash.h>

#define LOG_MODULE "i3"
#include "../../log.h"
#include "../../bar.h"

#include "dynlist-exposable.h"

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

    mtx_t lock;
    struct {
        struct ws_content *v;
        size_t count;
    } ws_content;

    struct {
        struct workspace *v;
        size_t count;
    } workspaces;
};

static struct workspace
workspace_from_json(const struct json_object *json)
{
    assert(json_object_is_type(json, json_type_object));
    struct json_object *name = json_object_object_get(json, "name");
    struct json_object *output = json_object_object_get(json, "output");
    const struct json_object *visible = json_object_object_get(json, "visible");
    const struct json_object *focused = json_object_object_get(json, "focused");
    const struct json_object *urgent = json_object_object_get(json, "urgent");

    return (struct workspace) {
        .name = strdup(json_object_get_string(name)),
        .output = strdup(json_object_get_string(output)),
        .visible = json_object_get_boolean(visible),
        .focused = json_object_get_boolean(focused),
        .urgent = json_object_get_boolean(urgent),
    };
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
    assert(json_object_is_type(json, json_type_object));

    struct json_object *version = json_object_object_get(json, "human_readable");

    assert(version != NULL);
    assert(json_object_is_type(version, json_type_string));
    LOG_INFO("connected to i3: %s", json_object_get_string(version));
    return true;
}

static bool
handle_subscribe_reply(struct private *m, const struct json_object *json)
{
    assert(json_object_is_type(json, json_type_object));

    const struct json_object *success = json_object_object_get(json, "success");

    assert(success != NULL);
    assert(json_object_is_type(success, json_type_boolean));
    assert(json_object_get_boolean(success));

    return json_object_get_boolean(success);
}

static bool
handle_get_workspaces_reply(struct private *m, const struct json_object *json)
{
    assert(json_object_is_type(json, json_type_array));

    workspaces_free(m);

    size_t count = json_object_array_length(json);
    m->workspaces.count = count;
    m->workspaces.v = malloc(count * sizeof(m->workspaces.v[0]));

    for (size_t i = 0; i < count; i++) {
        m->workspaces.v[i] = workspace_from_json(json_object_array_get_idx(json, i));
        LOG_DBG("#%zu: %s", i, m->workspaces.v[i].name);
    }

    return true;
}

static bool
handle_workspace_event(struct private *m, const struct json_object *json)
{
    assert(json_object_is_type(json, json_type_object));
    struct json_object *change = json_object_object_get(json, "change");
    const struct json_object *current = json_object_object_get(json, "current");
    const struct json_object *old = json_object_object_get(json, "old");

    const char *change_str = json_object_get_string(change);

    if (strcmp(change_str, "init") == 0) {
        struct json_object *current_name = json_object_object_get(current, "name");
        assert(workspace_lookup(m, json_object_get_string(current_name)) == NULL);

        struct workspace ws = workspace_from_json(current);
        workspace_add(m, ws);
    }

    else if (strcmp(change_str, "empty") == 0) {
        struct json_object *current_name = json_object_object_get(current, "name");
        assert(workspace_lookup(m, json_object_get_string(current_name)) != NULL);
        workspace_del(m, json_object_get_string(current_name));
    }

    else if (strcmp(change_str, "focus") == 0) {
        struct json_object *current_name = json_object_object_get(current, "name");
        struct workspace *w = workspace_lookup(m, json_object_get_string(current_name));
        assert(w != NULL);

        /* Mark all workspaces on current's output invisible */
        for (size_t i = 0; i < m->workspaces.count; i++) {
            struct workspace *ws = &m->workspaces.v[i];
            if (strcmp(ws->output, w->output) == 0)
                ws->visible = false;
        }

        const struct json_object *urgent = json_object_object_get(current, "urgent");
        w->urgent = json_object_get_boolean(urgent);
        w->focused = true;
        w->visible = true;

        /* Old workspace is no longer focused */
        struct json_object *old_name = json_object_object_get(old, "name");
        struct workspace *old_w = workspace_lookup(m, json_object_get_string(old_name));
        if (old_w != NULL)
            old_w->focused = false;
    }

    else if (strcmp(change_str, "urgent") == 0)
        ;
    else if (strcmp(change_str, "reload") == 0)
        ;

    return true;
}

static int
run(struct module_run_context *ctx)
{
    struct private *m = ctx->module->private;

    char sock_path[1024];
    {
        FILE *out = popen("i3 --get-socketpath", "r");
        assert(out != NULL);

        fgets(sock_path, sizeof(sock_path), out);
        pclose(out);

        /* Strip newline */
        ssize_t len = strlen(sock_path);
        if (sock_path[len - 1] == '\n')
            sock_path[len - 1] = '\0';
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock != -1);

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    int r = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    assert(r != -1);

    send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_VERSION, NULL);
    send_pkg(sock, I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, NULL);
    send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\"]");

    char buf[1 * 1024 * 1024];  /* Some replies are *big*. TODO: grow dynamically */
    size_t buf_idx = 0;

    while (true) {
        struct pollfd fds[] = {
            {.fd = ctx->abort_fd, .events = POLLIN},
            {.fd = sock, .events = POLLIN}
        };

        int res = poll(fds, 2, -1);
        assert(res > 0);

        if (fds[0].revents & POLLIN)
            break;

        assert(fds[1].revents & POLLIN);
        assert(sizeof(buf) > buf_idx);

        ssize_t bytes = read(sock, &buf[buf_idx], sizeof(buf) - buf_idx);
        if (bytes < 0)
            break;

        buf_idx += bytes;

        while (buf_idx >= sizeof(i3_ipc_header_t)) {
            const i3_ipc_header_t *hdr = (const i3_ipc_header_t *)buf;
            assert(strncmp(hdr->magic, I3_IPC_MAGIC, sizeof(hdr->magic)) == 0);

            size_t total_size = sizeof(i3_ipc_header_t) + hdr->size;

            if (total_size > buf_idx)
                break;

            /* Json-c expects a NULL-terminated string */
            char json_str[hdr->size + 1];
            memcpy(json_str, &buf[sizeof(*hdr)], hdr->size);
            json_str[hdr->size] = '\0';
            //printf("raw: %s\n", json_str);

            json_tokener *tokener = json_tokener_new();
            struct json_object *json = json_tokener_parse(json_str);
            assert(json != NULL);

            mtx_lock(&m->lock);

            switch (hdr->type) {
            case I3_IPC_REPLY_TYPE_VERSION:
                handle_get_version_reply(m, json);
                break;

            case I3_IPC_REPLY_TYPE_SUBSCRIBE:
                handle_subscribe_reply(m, json);
                break;

            case I3_IPC_REPLY_TYPE_WORKSPACES:
                handle_get_workspaces_reply(m, json);
                ctx->module->bar->refresh(ctx->module->bar);
                break;

            case I3_IPC_EVENT_WORKSPACE:
                handle_workspace_event(m, json);
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

            mtx_unlock(&m->lock);

            json_object_put(json);
            json_tokener_free(tokener);

            assert(total_size <= buf_idx);
            memmove(buf, &buf[total_size], buf_idx - total_size);
            buf_idx -= total_size;
        }
    }

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

    mtx_destroy(&m->lock);
    free(m);
    free(mod);
}

static struct exposable *
content(const struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&m->lock);

    struct exposable *particles[m->workspaces.count];

    size_t particle_count = 0;
    for (size_t i = 0; i < m->workspaces.count; i++) {
        const struct workspace *ws = &m->workspaces.v[i];
        const struct ws_content *template = NULL;

        /* Lookup workspace in user supplied workspace particle templates */
        for (size_t j = 0; j < m->ws_content.count; j++) {
            const struct ws_content *c = &m->ws_content.v[j];
            if (strcmp(c->name, ws->name) == 0) {
                template = c;
                break;
            }
        }

        if (template == NULL) {
            LOG_WARN("no ws template for %s", ws->name);
            continue;
        }

        const char *state =
            ws->urgent ? "urgent" :
            ws->visible ? ws->focused ? "focused" : "unfocused" :
            "invisible";

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string("name", ws->name),
                tag_new_bool("visible", ws->visible),
                tag_new_bool("focused", ws->focused),
                tag_new_bool("urgent", ws->urgent),
                tag_new_string("state", state),
            },
            .count = 5,
        };

        particles[particle_count++] = template->content->instantiate(
            template->content, &tags);

        tag_set_destroy(&tags);
    }

    mtx_unlock(&m->lock);
    return dynlist_exposable_new(
        particles, particle_count, m->left_spacing, m->right_spacing);
}

struct module *
module_i3(struct i3_workspaces workspaces[], size_t workspace_count,
          int left_spacing, int right_spacing)
{
    struct private *m = malloc(sizeof(*m));

    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    mtx_init(&m->lock, mtx_plain);
    m->ws_content.count = workspace_count;
    m->ws_content.v = malloc(workspace_count * sizeof(m->ws_content.v[0]));

    for (size_t i = 0; i < workspace_count; i++) {
        m->ws_content.v[i].name = strdup(workspaces[i].name);
        m->ws_content.v[i].content = workspaces[i].content;
    }

    m->workspaces.v = NULL;
    m->workspaces.count = 0;

    struct module *mod = malloc(sizeof(*mod));
    mod->bar = NULL;
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->begin_expose = &module_default_begin_expose;
    mod->expose = &module_default_expose;
    mod->end_expose = &module_default_end_expose;
    return mod;
}
