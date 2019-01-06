#include "config-verify.h"

#include <string.h>
#include <assert.h>

#define LOG_MODULE "config:verify"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "tllist.h"

typedef tll(const char *) keychain_t;

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

        if (strcmp(sub_key, "size") == 0 ||
            strcmp(sub_key, "y_offset") == 0)
        {
            if (!verify_int(chain_push(chain, sub_key), it.value))
                return false;
        } else if (strcmp(sub_key, "family") == 0) {
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
verify_module_alsa(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "card") == 0 ||
            strcmp(key, "mixer") == 0)
        {
            if (!verify_string(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_backlight(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "name") == 0) {
            if (!verify_string(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_battery(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "name") == 0) {
            if (!verify_string(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "poll_interval") == 0) {
            if (!verify_int(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_clock(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_i3(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "spacing") == 0 ||
            strcmp(key, "left_spacing") == 0 ||
            strcmp(key, "right_spacing") == 0)
        {
            if (!verify_int(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_label(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_mpd(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "host") == 0) {
            if (!verify_string(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "port") == 0) {
            if (!verify_int(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_network(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "name") == 0) {
            if (!verify_string(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_removables(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "spacing") == 0  ||
            strcmp(key, "left_spacing") == 0 ||
            strcmp(key, "right_spacing") == 0)
        {
            if (!verify_int(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_xkb(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

    return true;
}

static bool
verify_module_xwindow(keychain_t *chain, const struct yml_node *node)
{
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", err_prefix(chain, node));
            return false;
        }

        if (strcmp(key, "content") == 0) {
            if (!verify_particle(chain_push(chain, key), it.value))
                return false;
        }

        else if (strcmp(key, "anchors") == 0) {
            /* Skip */
            chain_push(chain, key);
        }

        else {
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, node), key);
            return false;
        }

        chain_pop(chain);
    }

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

    static const struct {
        const char *name;
        bool (*verify_fun)(keychain_t *chain, const struct yml_node *node);
    } modules[] = {
        {"alsa", &verify_module_alsa},
        {"backlight", &verify_module_backlight},
        {"battery", &verify_module_battery},
        {"clock", &verify_module_clock},
        {"i3", &verify_module_i3},
        {"label", &verify_module_label},
        {"mpd", &verify_module_mpd},
        {"network", &verify_module_network},
        {"removables", &verify_module_removables},
        {"xkb", &verify_module_xkb},
        {"xwindow", &verify_module_xwindow},
    };

    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        if (strcmp(mod_name, modules[i].name) == 0) {
            if (!modules[i].verify_fun(chain_push(chain, mod_name), values))
                return false;
            chain_pop(chain);
            return true;
        }
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
