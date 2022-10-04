#include <string.h>
#include <unistd.h>

#define LOG_MODULE "sway-xkb"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

#include "i3-ipc.h"
#include "i3-common.h"

#define max(x, y) ((x) > (y) ? (x) : (y))

struct input {
    bool exists;
    char *identifier;
    char *layout;
};

struct private {
    struct particle *template;
    int left_spacing;
    int right_spacing;

    size_t num_inputs;
    size_t num_existing_inputs;
    struct input *inputs;

    bool dirty;
};

static void
free_input(struct input *input)
{
    free(input->identifier);
    free(input->layout);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->template->destroy(m->template);

    for (size_t i = 0; i < m->num_inputs; i++)
        free_input(&m->inputs[i]);
    free(m->inputs);

    free(m);
    module_default_destroy(mod);
}

static const char *
description(struct module *mod)
{
    return "sway-xkb";
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    assert(m->num_existing_inputs <= m->num_inputs);
    struct exposable *particles[max(m->num_existing_inputs, 1)];

    for (size_t i = 0, j = 0; i < m->num_inputs; i++) {
        const struct input *input = &m->inputs[i];

        if (!input->exists)
            continue;

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "id", input->identifier),
                tag_new_string(mod, "layout", input->layout),
            },
            .count = 2,
        };

        particles[j++] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(
        particles, m->num_existing_inputs, m->left_spacing, m->right_spacing);
}

static bool
handle_input_reply(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    assert(m->num_existing_inputs == 0);

    for (size_t i = 0; i < json_object_array_length(json); i++) {
        struct json_object *obj = json_object_array_get_idx(json, i);

        struct json_object *identifier;
        if (!json_object_object_get_ex(obj, "identifier", &identifier))
            return false;

        const char *id = json_object_get_string(identifier);

        struct json_object *type;
        if (!json_object_object_get_ex(obj, "type", &type))
            return false;
        if (strcmp(json_object_get_string(type), "keyboard") != 0) {
            LOG_DBG("ignoring non-keyboard input '%s'", id);
            continue;
        }

        struct input *input = NULL;
        for (size_t i = 0; i < m->num_inputs; i++) {
            struct input *maybe_input = &m->inputs[i];
            if (strcmp(maybe_input->identifier, id) == 0 && !maybe_input->exists)
            {
                input = maybe_input;

                LOG_DBG("adding: %s", id);

                mtx_lock(&mod->lock);
                input->exists = true;
                m->num_existing_inputs++;
                mtx_unlock(&mod->lock);
                break;
            }
        }

        if (input == NULL) {
            LOG_DBG("ignoring xkb_layout change for input '%s'", id);
            continue;
        }

        /* Get current/active layout */
        struct json_object *layout;
        if (!json_object_object_get_ex(
                obj, "xkb_active_layout_name", &layout))
            return false;

        const char *new_layout_str = json_object_get_string(layout);

        mtx_lock(&mod->lock);

        assert(input != NULL);
        free(input->layout);
        input->layout = strdup(new_layout_str);

        m->dirty = true;
        mtx_unlock(&mod->lock);
    }

    return true;
}

static bool
handle_input_event(int type, const struct json_object *json, void *_mod)
{
    struct module *mod = _mod;
    struct private *m = mod->private;

    struct json_object *change;
    if (!json_object_object_get_ex(json, "change", &change))
        return false;

    const char *change_str = json_object_get_string(change);
    bool is_layout = strcmp(change_str, "xkb_layout") == 0;
    bool is_removed = strcmp(change_str, "removed") == 0;
    bool is_added = strcmp(change_str, "added") == 0;

    if (!is_layout && !is_removed && !is_added)
        return true;

    struct json_object *obj;
    if (!json_object_object_get_ex(json, "input", &obj))
        return false;

    struct json_object *identifier;
    if (!json_object_object_get_ex(obj, "identifier", &identifier))
        return false;

    const char *id = json_object_get_string(identifier);

    struct json_object *input_type;
    if (!json_object_object_get_ex(obj, "type", &input_type))
        return false;
    if (strcmp(json_object_get_string(input_type), "keyboard") != 0) {
        LOG_DBG("ignoring non-keyboard input '%s'", id);
        return true;
    }

    struct input *input = NULL;
    for (size_t i = 0; i < m->num_inputs; i++) {
        struct input *maybe_input = &m->inputs[i];
        if (strcmp(maybe_input->identifier, id) == 0) {
            input = maybe_input;
            break;
        }
    }

    if (input == NULL) {
        LOG_DBG("ignoring xkb_layout change for input '%s'", id);
        return true;
    }

    if (is_removed) {
        if (input->exists) {
            LOG_DBG("removing: %s", id);

            mtx_lock(&mod->lock);
            input->exists = false;
            m->num_existing_inputs--;
            m->dirty = true;
            mtx_unlock(&mod->lock);
        }
        return true;
    }

    if (is_added) {
        if (!input->exists) {
            LOG_DBG("adding: %s", id);

            mtx_lock(&mod->lock);
            input->exists = true;
            m->num_existing_inputs++;
            m->dirty = true;
            mtx_unlock(&mod->lock);
        }

        /* “fallthrough”, to query current/active layout */
    }

    /* Get current/active layout */
    struct json_object *layout;
    if (!json_object_object_get_ex(
            obj, "xkb_active_layout_name", &layout))
        return false;

    const char *new_layout_str = json_object_get_string(layout);

    mtx_lock(&mod->lock);

    assert(input != NULL);
    free(input->layout);
    input->layout = strdup(new_layout_str);

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
    if (getenv("SWAYSOCK") == NULL) {
        LOG_ERR("sway does not appear to be running");
        return 1;
    }

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

    i3_send_pkg(sock, 100 /* IPC_GET_INPUTS */, NULL);
    i3_send_pkg(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"input\"]");

    static const struct i3_ipc_callbacks callbacks = {
        .burst_done = &burst_done,
        .reply_inputs = &handle_input_reply,
        .event_input = &handle_input_event,
    };

    bool ret = i3_receive_loop(mod->abort_fd, sock, &callbacks, mod);
    close(sock);
    return ret ? 0 : 1;
}

static struct module *
sway_xkb_new(struct particle *template, const char *identifiers[],
             size_t num_identifiers, int left_spacing, int right_spacing)
{
    struct private *m = calloc(1, sizeof(*m));
    m->template = template;
    m->left_spacing = left_spacing;
    m->right_spacing = right_spacing;

    m->num_inputs = num_identifiers;
    m->inputs = calloc(num_identifiers, sizeof(m->inputs[0]));

    for (size_t i = 0; i < num_identifiers; i++) {
        m->inputs[i].identifier = strdup(identifiers[i]);
        m->inputs[i].layout = strdup("");
    }

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

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    const struct yml_node *ids = yml_get_value(node, "identifiers");
    const size_t num_ids = yml_list_length(ids);
    const char *identifiers[num_ids];

    size_t i = 0;
    for (struct yml_list_iter it = yml_list_iter(ids);
         it.node != NULL;
         yml_list_next(&it), i++)
    {
        identifiers[i] = yml_value_as_string(it.node);
    }

    return sway_xkb_new(
        conf_to_particle(c, inherited), identifiers, num_ids, left, right);
}

static bool
verify_identifiers(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: identifiers must be a list of strings",
                conf_err_prefix(chain, node));
        return false;
    }

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!conf_verify_string(chain, it.node))
            return false;
    }

    return true;
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"spacing", false, &conf_verify_unsigned},
        {"left-spacing", false, &conf_verify_unsigned},
        {"right-spacing", false, &conf_verify_unsigned},
        {"identifiers", true, &verify_identifiers},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_sway_xkb_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_sway_xkb_iface"))) ;
#endif
