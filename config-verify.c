#include "config-verify.h"

#include <string.h>
#include <assert.h>

#define LOG_MODULE "config:verify"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "tllist.h"

typedef tll(const char *) keychain_t;

struct attr_info {
    const char *name;
    bool required;
    bool (*verify)(keychain_t *chain, const struct yml_node *node);
};

static keychain_t *
chain_push(keychain_t *chain, const char *key)
{
    tll_push_back(*chain, key);
    return chain;
}

static void
chain_pop(keychain_t *chain)
{
    tll_pop_back(*chain);
}

static const char *
err_prefix(const keychain_t *chain, const struct yml_node *node)
{
    static char msg[4096];
    int idx = 0;

    tll_foreach(*chain, key)
        idx += snprintf(&msg[idx], sizeof(msg) - idx, "%s.", key->item);

    /* Remove trailing "." */
    msg[idx - 1] = '\0';
    return msg;
}

static bool
verify_string(keychain_t *chain, const struct yml_node *node)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", err_prefix(chain, node));
        return false;
    }

    return true;
}

static bool
verify_int(keychain_t *chain, const struct yml_node *node)
{
    if (yml_value_is_int(node))
        return true;

    LOG_ERR("%s: value is not an integer: '%s'",
            err_prefix(chain, node), yml_value_as_string(node));
    return false;
}

static bool
verify_enum(keychain_t *chain, const struct yml_node *node,
            const char *values[], size_t count)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", err_prefix(chain, node));
        return false;
    }

    for (size_t i = 0; s != NULL && i < count; i++) {
        if (strcmp(s, values[i]) == 0)
            return true;
    }

    LOG_ERR("%s: value must be one of:", err_prefix(chain, node));
    for (size_t i = 0; i < count; i++)
        LOG_ERR("  %s", values[i]);

    return false;
}

static bool
verify_color(keychain_t *chain, const struct yml_node *node)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", err_prefix(chain, node));
        return false;
    }

    unsigned int r, g, b, a;
    int v = sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a);

    if (strlen(s) != 8 || v != 4) {
        LOG_ERR("%s: value must be a color ('rrggbbaa', e.g ff00ffff)",
                err_prefix(chain, node));
        return false;
    }

    return true;
}

static bool
verify_font(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR("%s: must be a dictionary", err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *sub_key = yml_value_as_string(it.key);
        if (sub_key == NULL) {
            LOG_ERR("%s: font: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(sub_key, "family") == 0) {
            if (!verify_string(chain_push(chain, sub_key), it.value))
                return false;
        } else {
            LOG_ERR("%s: font: invalid key: %s", err_prefix(chain, node), sub_key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_dict(keychain_t *chain, const struct yml_node *node,
            const struct attr_info info[], size_t count)
{
    if (!yml_is_dict(node)) {
        LOG_ERR("%s: must be a dictionary", err_prefix(chain, node));
        return false;
    }

    bool exists[count];
    memset(exists, 0, sizeof(exists));

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        const struct attr_info *attr = NULL;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(info[i].name, key) == 0) {
                attr = &info[i];
                exists[i] = true;
                break;
            }
        }

        if (attr == NULL) {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        if (attr->verify == NULL)
            continue;

        if (!attr->verify(chain_push(chain, key), it.value))
            return false;
        chain_pop(chain);
    }

    for (size_t i = 0; i < count; i++) {
        if (!info[i].required || exists[i])
            continue;

        LOG_ERR("%s: missing required key: %s",
                err_prefix(chain, node), info[i].name);
        return false;
    }

    return true;
}

static bool
verify_border(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR("bar: border: must be a dictionary");
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "width") == 0) {
            if (!verify_int(chain_push(chain, key), it.value))
                return false;
        } else if (strcmp(key, "color") == 0) {
            if (!verify_color(chain_push(chain, key), it.value))
                return false;
        } else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_particle(keychain_t *chain, const struct yml_node *node)
{
    return true;
}

static bool
verify_module(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node) || yml_dict_length(node) != 1) {
        LOG_ERR("%s: module must be a dictionary with a single key; "
                "the name of the module", err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter m = yml_dict_iter(node);
    const struct yml_node *module = m.key;
    const struct yml_node *values = m.value;

    const char *mod_name = yml_value_as_string(module);
    if (mod_name == NULL) {
        LOG_ERR("%s: module name must be a string", err_prefix(chain, node));
        return false;
    }

    static const struct attr_info alsa[] = {
        {"card", true, &verify_string},
        {"mixer", true, &verify_string},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info backlight[] = {
        {"name", true, &verify_string},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info battery[] = {
        {"name", true, &verify_string},
        {"poll_interval", false, &verify_int},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info clock[] = {
        {"date-format", false, &verify_string},
        {"time-format", false, &verify_string},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info label[] = {
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info mpd[] = {
        {"host", true, &verify_string},
        {"port", false, &verify_int},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info i3[] = {
        {"spacing", false, &verify_int},
        {"left_spacing", false, &verify_int},
        {"right_spacing", false, &verify_int},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info network[] = {
        {"name", true, &verify_string},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info removables[] = {
        {"spacing", false, &verify_int},
        {"left_spacing", false, &verify_int},
        {"right_spacing", false, &verify_int},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info xkb[] = {
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info xwindow[] = {
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct {
        const char *name;
        const struct attr_info *attrs;
        size_t count;
    } modules[] = {
        {"alsa", alsa, sizeof(alsa) / sizeof(alsa[0])},
        {"backlight", backlight, sizeof(backlight) / sizeof(backlight[0])},
        {"battery", battery, sizeof(battery) / sizeof(battery[0])},
        {"clock", clock, sizeof(clock) / sizeof(clock[0])},
        {"i3", i3, sizeof(i3) / sizeof(i3[0])},
        {"label", label, sizeof(label) / sizeof(label[0])},
        {"mpd", mpd, sizeof(mpd) / sizeof(mpd[0])},
        {"network", network, sizeof(network) / sizeof(network[0])},
        {"removables", removables, sizeof(removables) / sizeof(removables[0])},
        {"xkb", xkb, sizeof(xkb) / sizeof(xkb[0])},
        {"xwindow", xwindow, sizeof(xwindow) / sizeof(xwindow[0])},
    };

    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        if (strcmp(modules[i].name, mod_name) != 0)
            continue;

        if (!verify_dict(chain_push(chain, mod_name), values,
                         modules[i].attrs, modules[i].count))
        {
            return false;
        }

        chain_pop(chain);
        return true;
    }

    LOG_ERR("%s: invalid module name: %s", err_prefix(chain, node), mod_name);
    return false;
}

static bool
verify_module_list(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: must be a list of modules", err_prefix(chain, node));
        return false;
    }

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!verify_module(chain, it.node))
            return false;
    }

    return true;
}

bool
config_verify_bar(const struct yml_node *bar)
{
    if (!yml_is_dict(bar)) {
        LOG_ERR("bar is not a dictionary");
        return false;
    }

    keychain_t chain = tll_init();
    chain_push(&chain, "bar");

    bool ret = false;

    for (struct yml_dict_iter it = yml_dict_iter(bar);
         it.key != NULL;
         yml_dict_next(&it))
    {
        if (!yml_is_scalar(it.key)) {
            LOG_ERR("bar: key is not a scalar");
            goto err;
        }

        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("bar: key must be a string");
            goto err;
        }

        if (strcmp(key, "height") == 0 ||
            strcmp(key, "spacing") == 0 ||
            strcmp(key, "left_spacing") == 0 ||
            strcmp(key, "right_spacing") == 0 ||
            strcmp(key, "margin") == 0 ||
            strcmp(key, "left_margin") == 0 ||
            strcmp(key, "right_margin") == 0)
        {
            if (!verify_int(chain_push(&chain, key), it.value))
                goto err;
        }

        else if (strcmp(key, "location") == 0) {
            if (!verify_enum(chain_push(&chain, key), it.value,
                             (const char *[]){"top", "bottom"}, 2))
                goto err;
        }

        else if (strcmp(key, "background") == 0) {
            if (!verify_color(chain_push(&chain, key), it.value))
                goto err;
        }

        else if (strcmp(key, "border") == 0) {
            if (!verify_border(chain_push(&chain, key), it.value))
                goto err;
        }

        else if (strcmp(key, "font") == 0) {
            if (!verify_font(chain_push(&chain, key), it.value))
                goto err;
        }

        else if (strcmp(key, "left") == 0 ||
                 strcmp(key, "center") == 0 ||
                 strcmp(key, "right") == 0)
        {
            if (!verify_module_list(chain_push(&chain, key), it.value))
                goto err;
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(&chain, bar), key);
            goto err;
        }

        LOG_DBG("%s: verified", key);
        chain_pop(&chain);
    }

    ret = true;

err:
    tll_free(chain);
    return ret;
}
