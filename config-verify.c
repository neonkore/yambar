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

    idx += snprintf(&msg[idx], sizeof(msg) - idx, "%zu:%zu: ",
                    yml_source_line(node), yml_source_column(node));

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
            LOG_ERR("%s: key must be a string", err_prefix(chain, it.key));
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
            LOG_ERR("%s: invalid key: %s", err_prefix(chain, it.key), key);
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

        LOG_ERR("%s: missing required key: %s", err_prefix(chain, node), info[i].name);
        return false;
    }

    return true;
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
    static const struct attr_info attrs[] = {
        {"family", true, &verify_string},
    };

    return verify_dict(chain, node, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static bool verify_decoration(keychain_t *chain, const struct yml_node *node);

static bool
verify_decoration_stack(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: must be a list of decorations", err_prefix(chain, node));
        return false;
    }

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!verify_decoration(chain, it.node))
            return false;
    }

    return true;
}

static bool
verify_decoration(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_dict(node));

    if (yml_dict_length(node) != 1) {
        LOG_ERR("%s: decoration must be a dictionary with a single key; "
                "the name of the particle", err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter p = yml_dict_iter(node);
    const struct yml_node *deco = p.key;
    const struct yml_node *values = p.value;

    const char *deco_name = yml_value_as_string(deco);
    if (deco_name == NULL) {
        LOG_ERR("%s: decoration name must be a string", err_prefix(chain, deco));
        return false;
    }

    if (strcmp(deco_name, "stack") == 0) {
        bool ret = verify_decoration_stack(chain_push(chain, deco_name), values);
        chain_pop(chain);
        return ret;
    }

    static const struct attr_info background[] = {
        {"color", true, &verify_color},
    };

    static const struct attr_info underline[] = {
        {"size", true, &verify_int},
        {"color", true, &verify_color},
    };

    static const struct {
        const char *name;
        const struct attr_info *attrs;
        size_t count;
    } decos[] = {
        {"background", background, sizeof(background) / sizeof(background[0])},
        {"underline", underline, sizeof(underline) / sizeof(underline[0])},
    };

    for (size_t i = 0; i < sizeof(decos) / sizeof(decos[0]); i++) {
        if (strcmp(decos[i].name, deco_name) != 0)
            continue;

        if (!verify_dict(chain_push(chain, deco_name),
                         values, decos[i].attrs, decos[i].count))
        {
            return false;
        }

        chain_pop(chain);
        return true;
    }

    LOG_ERR(
        "%s: invalid decoration name: %s", err_prefix(chain, deco), deco_name);
    return false;
}

static bool verify_particle(keychain_t *chain, const struct yml_node *node);

static bool
verify_list_items(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_list(node));

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!verify_particle(chain, it.node))
            return false;
    }

    return true;
}

static bool
verify_map_values(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string (a i3 workspace name)",
                    err_prefix(chain, it.key));
            return false;
        }

        if (!verify_particle(chain_push(chain, key), it.value))
            return false;

        chain_pop(chain);
    }

    return true;
}

static bool
verify_particle_dictionary(keychain_t *chain, const struct yml_node *node)
{
    assert(yml_is_dict(node));

    if (yml_dict_length(node) != 1) {
        LOG_ERR("%s: particle must be a dictionary with a single key; "
                "the name of the particle", err_prefix(chain, node));
        return false;
    }

    struct yml_dict_iter p = yml_dict_iter(node);
    const struct yml_node *particle = p.key;
    const struct yml_node *values = p.value;

    const char *particle_name = yml_value_as_string(particle);
    if (particle_name == NULL) {
        LOG_ERR("%s: particle name must be a string", err_prefix(chain, particle));
        return false;
    }

#define COMMON_ATTRS                          \
    {"margin", false, &verify_int},           \
    {"left-margin", false, &verify_int},      \
    {"right-margin", false, &verify_int},     \
    {"on-click", false, &verify_string},

    static const struct attr_info empty[] = {
        COMMON_ATTRS
    };

    static const struct attr_info list[] = {
        {"items", true, &verify_list_items},
        {"spacing", false, &verify_int},
        {"left-spacing", false, &verify_int},
        {"right-spacing", false, &verify_int},
        COMMON_ATTRS
    };

    static const struct attr_info map[] = {
        {"tag", true, &verify_string},
        {"values", true, &verify_map_values},
        {"default", false, &verify_particle},
        COMMON_ATTRS
    };

    static const struct attr_info progress_bar[] = {
        {"tag", true, &verify_string},
        {"length", true, &verify_int},
        /* TODO: make these optional? Default to empty */
        {"start", true, &verify_particle},
        {"end", true, &verify_particle},
        {"fill", true, &verify_particle},
        {"empty", true, &verify_particle},
        {"indicator", true, &verify_particle},
        COMMON_ATTRS
    };

    static const struct attr_info ramp[] = {
        {"tag", true, &verify_string},
        {"items", true, &verify_list_items},
        COMMON_ATTRS
    };

    static const struct attr_info string[] = {
        {"text", true, &verify_string},
        {"max", false, &verify_int},
        {"font", false, &verify_font},
        {"foreground", false, &verify_color},
        {"deco", false, &verify_decoration},
        COMMON_ATTRS
    };

#undef COMMON_ATTRS

    static const struct {
        const char *name;
        const struct attr_info *attrs;
        size_t count;
    } particles[] = {
        {"empty", empty, sizeof(empty) / sizeof(empty[0])},
        {"list", list, sizeof(list) / sizeof(list[0])},
        {"map", map, sizeof(map) / sizeof(map[0])},
        {"progress-bar", progress_bar, sizeof(progress_bar) / sizeof(progress_bar[0])},
        {"ramp", ramp, sizeof(ramp) / sizeof(ramp[0])},
        {"string", string, sizeof(string) / sizeof(string[0])},
    };

    for (size_t i = 0; i < sizeof(particles) / sizeof(particles[0]); i++) {
        if (strcmp(particles[i].name, particle_name) != 0)
            continue;

        if (!verify_dict(chain_push(chain, particle_name), values,
                         particles[i].attrs, particles[i].count))
        {
            return false;
        }

        chain_pop(chain);
        return true;
    }

    LOG_ERR(
        "%s: invalid particle name: %s", err_prefix(chain, particle), particle_name);
    return false;
}

static bool
verify_particle(keychain_t *chain, const struct yml_node *node)
{
    if (yml_is_dict(node))
        return verify_particle_dictionary(chain, node);
    else if (yml_is_list(node))
        return verify_list_items(chain, node);
    else {
        LOG_ERR("%s: particle must be either a dictionary or a list",
                err_prefix(chain, node));
        return false;
    }
}

static bool
verify_i3_content(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string (a i3 workspace name)",
                    err_prefix(chain, it.key));
            return false;
        }

        if (!verify_particle(chain_push(chain, key), it.value))
            return false;

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
        LOG_ERR("%s: module name must be a string", err_prefix(chain, module));
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
        {"poll-interval", false, &verify_int},
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
        {"left-spacing", false, &verify_int},
        {"right-spacing", false, &verify_int},
        {"content", true, &verify_i3_content},
        {"anchors", false, NULL},
    };

    static const struct attr_info network[] = {
        {"name", true, &verify_string},
        {"content", true, &verify_particle},
        {"anchors", false, NULL},
    };

    static const struct attr_info removables[] = {
        {"spacing", false, &verify_int},
        {"left-spacing", false, &verify_int},
        {"right-spacing", false, &verify_int},
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

    LOG_ERR("%s: invalid module name: %s", err_prefix(chain, module), mod_name);
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

static bool
verify_bar_border(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"width", true, &verify_int},
        {"color", true, &verify_color},
    };

    return verify_dict(chain, node, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static bool
verify_bar_location(keychain_t *chain, const struct yml_node *node)
{
    return verify_enum(chain, node, (const char *[]){"top", "bottom"}, 2);
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

    static const struct attr_info attrs[] = {
        {"height", true, &verify_int},
        {"location", true, &verify_bar_location},
        {"background", true, &verify_color},

        {"spacing", false, &verify_int},
        {"left-spacing", false, &verify_int},
        {"right-spacing", false, &verify_int},

        {"margin", false, &verify_int},
        {"left_margin", false, &verify_int},
        {"right_margin", false, &verify_int},

        {"border", false, &verify_bar_border},
        {"font", false, &verify_font},

        {"left", false, &verify_module_list},
        {"center", false, &verify_module_list},
        {"right", false, &verify_module_list},
    };

    bool ret = verify_dict(&chain, bar, attrs, sizeof(attrs) / sizeof(attrs[0]));
    tll_free(chain);
    return ret;
}
