#include "config.h"

#include <string.h>
#include <assert.h>

#include <tllist.h>

#define LOG_MODULE "config:verify"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "plugin.h"

const char *
conf_err_prefix(const keychain_t *chain, const struct yml_node *node)
{
    static char msg[4096];
    int idx = 0;

    idx += snprintf(&msg[idx], sizeof(msg) - idx, "%zu:%zu: ",
                    yml_source_line(node), yml_source_column(node));

    tll_foreach(*chain, key)
        idx += snprintf(&msg[idx], sizeof(msg) - idx, "%s.", key->item);

    /* Remove trailing "." */
    msg[idx - 1] = '\0';
    return msg;
}

bool
conf_verify_string(keychain_t *chain, const struct yml_node *node)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", conf_err_prefix(chain, node));
        return false;
    }

    return true;
}

bool
conf_verify_int(keychain_t *chain, const struct yml_node *node)
{
    if (yml_value_is_int(node))
        return true;

    LOG_ERR("%s: value is not an integer: '%s'",
            conf_err_prefix(chain, node), yml_value_as_string(node));
    return false;
}

bool
conf_verify_enum(keychain_t *chain, const struct yml_node *node,
                 const char *values[], size_t count)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", conf_err_prefix(chain, node));
        return false;
    }

    for (size_t i = 0; s != NULL && i < count; i++) {
        if (strcmp(s, values[i]) == 0)
            return true;
    }

    LOG_ERR("%s: value must be one of:", conf_err_prefix(chain, node));
    for (size_t i = 0; i < count; i++)
        LOG_ERR("  %s", values[i]);

    return false;
}

bool
conf_verify_dict(keychain_t *chain, const struct yml_node *node,
                 const struct attr_info info[])
{
    if (!yml_is_dict(node)) {
        LOG_ERR("%s: must be a dictionary", conf_err_prefix(chain, node));
        return false;
    }

    /* Count the attributes */
    size_t count = 0;
    for (; info[count].name != NULL; count++)
        ;

    bool exists[count];
    memset(exists, 0, sizeof(exists));

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", conf_err_prefix(chain, it.key));
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
            LOG_ERR("%s: invalid key: %s", conf_err_prefix(chain, it.key), key);
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

        LOG_ERR("%s: missing required key: %s", conf_err_prefix(chain, node), info[i].name);
        return false;
    }

    return true;
}

bool
conf_verify_color(keychain_t *chain, const struct yml_node *node)
{
    const char *s = yml_value_as_string(node);
    if (s == NULL) {
        LOG_ERR("%s: value must be a string", conf_err_prefix(chain, node));
        return false;
    }

    unsigned int r, g, b, a;
    int v = sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a);

    if (strlen(s) != 8 || v != 4) {
        LOG_ERR("%s: value must be a color ('rrggbbaa', e.g ff00ffff)",
                conf_err_prefix(chain, node));
        return false;
    }

    return true;
}


bool
conf_verify_font(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_scalar(node)) {
        LOG_ERR("%s: font must be a fontconfig-formatted string",
                conf_err_prefix(chain, node));
        return false;
    }

    return true;
}

bool
conf_verify_decoration(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_dict(node));

    if (yml_dict_length(node) != 1) {
        LOG_ERR("%s: decoration must be a dictionary with a single key; "
                "the name of the particle", conf_err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter p = yml_dict_iter(node);
    const struct yml_node *deco = p.key;
    const struct yml_node *values = p.value;

    const char *deco_name = yml_value_as_string(deco);
    if (deco_name == NULL) {
        LOG_ERR("%s: decoration name must be a string", conf_err_prefix(chain, deco));
        return false;
    }

    const struct deco_iface *iface = plugin_load_deco(deco_name);
    if (iface == NULL) {
        LOG_ERR("%s: invalid decoration name: %s",
                conf_err_prefix(chain, deco), deco_name);
        return false;
    }

    chain_push(chain, deco_name);
    if (!iface->verify_conf(chain, values))
        return false;

    chain_pop(chain);
    return true;
}

bool
conf_verify_particle_list_items(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_list(node));

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!conf_verify_particle(chain, it.node))
            return false;
    }

    return true;
}

static bool
conf_verify_particle_dictionary(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_dict(node));

    if (yml_dict_length(node) != 1) {
        LOG_ERR("%s: particle must be a dictionary with a single key; "
                "the name of the particle", conf_err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter p = yml_dict_iter(node);
    const struct yml_node *particle = p.key;
    const struct yml_node *values = p.value;

    const char *particle_name = yml_value_as_string(particle);
    if (particle_name == NULL) {
        LOG_ERR("%s: particle name must be a string", conf_err_prefix(chain, particle));
        return false;
    }

    const struct particle_iface *iface = plugin_load_particle(particle_name);
    if (iface == NULL) {
        LOG_ERR("%s: invalid particle name: %s",
                conf_err_prefix(chain, particle), particle_name);
        return false;
    }

    assert(iface->verify_conf != NULL);

    chain_push(chain, particle_name);
    if (!iface->verify_conf(chain, values))
        return false;

    chain_pop(chain);
    return true;
}

bool
conf_verify_particle(keychain_t *chain, const struct yml_node *node)
{
    if (yml_is_dict(node))
        return conf_verify_particle_dictionary(chain, node);
    else if (yml_is_list(node))
        return conf_verify_particle_list_items(chain, node);
    else {
        LOG_ERR("%s: particle must be either a dictionary or a list",
                conf_err_prefix(chain, node));
        return false;
    }
}


static bool
verify_module(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node) || yml_dict_length(node) != 1) {
        LOG_ERR("%s: module must be a dictionary with a single key; "
                "the name of the module", conf_err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter m = yml_dict_iter(node);
    const struct yml_node *module = m.key;
    const struct yml_node *values = m.value;

    const char *mod_name = yml_value_as_string(module);
    if (mod_name == NULL) {
        LOG_ERR("%s: module name must be a string", conf_err_prefix(chain, module));
        return false;
    }

    const struct module_iface *iface = plugin_load_module(mod_name);
    if (iface == NULL) {
        LOG_ERR(
            "%s: invalid module name: %s", conf_err_prefix(chain, node), mod_name);
        return false;
    }

    assert(iface->verify_conf != NULL);

    chain_push(chain, mod_name);
    if (!iface->verify_conf(chain, values))
        return false;

    chain_pop(chain);
    return true;
}

static bool
verify_module_list(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: must be a list of modules", conf_err_prefix(chain, node));
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

static bool
verify_bar_border(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"width", false, &conf_verify_int},
        {"color", false, &conf_verify_color},
        {"margin", false, &conf_verify_int},
        {"left-margin", false, &conf_verify_int},
        {"right-margin", false, &conf_verify_int},
        {"top-margin", false, &conf_verify_int},
        {"bottom-margin", false, &conf_verify_int},
        {NULL, false, NULL},
    };

    return conf_verify_dict(chain, node, attrs);
}

static bool
verify_bar_location(keychain_t *chain, const struct yml_node *node)
{
    return conf_verify_enum(chain, node, (const char *[]){"top", "bottom"}, 2);
}

bool
conf_verify_bar(const struct yml_node *bar)
{
    if (!yml_is_dict(bar)) {
        LOG_ERR("bar is not a dictionary");
        return false;
    }

    keychain_t chain = tll_init();
    chain_push(&chain, "bar");

    static const struct attr_info attrs[] = {
        {"height", true, &conf_verify_int},
        {"location", true, &verify_bar_location},
        {"background", true, &conf_verify_color},

        {"monitor", false, &conf_verify_string},

        {"spacing", false, &conf_verify_int},
        {"left-spacing", false, &conf_verify_int},
        {"right-spacing", false, &conf_verify_int},

        {"margin", false, &conf_verify_int},
        {"left_margin", false, &conf_verify_int},
        {"right_margin", false, &conf_verify_int},

        {"border", false, &verify_bar_border},
        {"font", false, &conf_verify_font},
        {"foreground", false, &conf_verify_color},

        {"left", false, &verify_module_list},
        {"center", false, &verify_module_list},
        {"right", false, &verify_module_list},

        {NULL, false, NULL},
    };

    bool ret = conf_verify_dict(&chain, bar, attrs);
    tll_free(chain);
    return ret;
}
