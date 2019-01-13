#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <dlfcn.h>

#include "color.h"

#include "decoration.h"
#include "decorations/background.h"
#include "decorations/stack.h"
#include "decorations/underline.h"

#include "module.h"
#include "config-verify.h"
#include "plugin.h"

static uint8_t
hex_nibble(char hex)
{
    assert((hex >= '0' && hex <= '9') ||
           (hex >= 'a' && hex <= 'f') ||
           (hex >= 'A' && hex <= 'F'));

    if (hex >= '0' && hex <= '9')
        return hex - '0';
    else if (hex >= 'a' && hex <= 'f')
        return hex - 'a' + 10;
    else
        return hex - 'A' + 10;
}

static uint8_t
hex_byte(const char hex[2])
{
    uint8_t upper = hex_nibble(hex[0]);
    uint8_t lower = hex_nibble(hex[1]);
    return upper << 4 | lower;
}

struct rgba
conf_to_color(const struct yml_node *node)
{
    const char *hex = yml_value_as_string(node);

    assert(hex != NULL);
    assert(strlen(hex) == 8);

    uint8_t red = hex_byte(&hex[0]);
    uint8_t green = hex_byte(&hex[2]);
    uint8_t blue = hex_byte(&hex[4]);
    uint8_t alpha = hex_byte(&hex[6]);

    struct rgba rgba = {
        (double)red / 255.0,
        (double)green / 255.0,
        (double)blue / 255.0,
        (double)alpha / 255.0
    };

    assert(rgba.red >= 0.0 && rgba.red <= 1.0);
    assert(rgba.green >= 0.0 && rgba.green <= 1.0);
    assert(rgba.blue >= 0.0 && rgba.blue <= 1.0);
    assert(rgba.alpha >= 0.0 && rgba.alpha <= 1.0);

    return rgba;
}

struct font *
conf_to_font(const struct yml_node *node)
{
    const struct yml_node *family = yml_get_value(node, "family");
    return font_new(family != NULL ? yml_value_as_string(family) : "monospace");
}

static struct deco *
deco_background_from_config(const struct yml_node *node)
{
    const struct yml_node *color = yml_get_value(node, "color");
    return deco_background(conf_to_color(color));
}

static struct deco *
deco_underline_from_config(const struct yml_node *node)
{
    const struct yml_node *size = yml_get_value(node, "size");
    const struct yml_node *color = yml_get_value(node, "color");
    return deco_underline(yml_value_as_int(size), conf_to_color(color));
}

static struct deco *deco_from_config(const struct yml_node *node);

static struct deco *
deco_stack_from_config(const struct yml_node *node)
{
    size_t count = yml_list_length(node);

    struct deco *decos[count];
    size_t idx = 0;

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        decos[idx] = deco_from_config(it.node);
    }

    return deco_stack(decos, count);
}

static struct deco *
deco_from_config(const struct yml_node *node)
{
    struct yml_dict_iter it = yml_dict_iter(node);
    const struct yml_node *deco_type = it.key;
    const struct yml_node *deco_data = it.value;

    const char *type = yml_value_as_string(deco_type);

    if (strcmp(type, "background") == 0)
        return deco_background_from_config(deco_data);
    else if (strcmp(type, "underline") == 0)
        return deco_underline_from_config(deco_data);
    else if (strcmp(type, "stack") == 0)
        return deco_stack_from_config(deco_data);
    else
        assert(false);

    return NULL;
}

static struct particle *
particle_simple_list_from_config(const struct yml_node *node,
                                 const struct font *parent_font)
{
    size_t count = yml_list_length(node);
    struct particle *parts[count];

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = conf_to_particle(it.node, parent_font);
    }

    /* Lazy-loaded function pointer to particle_list_new() */
    static struct particle *(*particle_list_new)(
        struct particle *particles[], size_t count,
        int left_spacing, int right_spacing, int left_margin, int right_margin,
        const char *on_click_template) = NULL;

    if (particle_list_new == NULL) {
        const struct plugin *plug = plugin_load("list", PLUGIN_PARTICLE);
        particle_list_new = dlsym(plug->lib, "particle_list_new");
        assert(particle_list_new != NULL);
    }

    return particle_list_new(parts, count, 0, 2, 0, 0, NULL);
}

struct particle *
conf_to_particle(const struct yml_node *node, const struct font *parent_font)
{
    if (yml_is_list(node))
        return particle_simple_list_from_config(node, parent_font);

    struct yml_dict_iter pair = yml_dict_iter(node);
    const char *type = yml_value_as_string(pair.key);

    const struct yml_node *margin = yml_get_value(pair.value, "margin");
    const struct yml_node *left_margin = yml_get_value(pair.value, "left-margin");
    const struct yml_node *right_margin = yml_get_value(pair.value, "right-margin");
    const struct yml_node *on_click = yml_get_value(pair.value, "on-click");

    int left = margin != NULL ? yml_value_as_int(margin) :
        left_margin != NULL ? yml_value_as_int(left_margin) : 0;
    int right = margin != NULL ? yml_value_as_int(margin) :
        right_margin != NULL ? yml_value_as_int(right_margin) : 0;

    const char *on_click_template
        = on_click != NULL ? yml_value_as_string(on_click) : NULL;

    const struct particle_info *info = plugin_load_particle(type);
    assert(info != NULL);

    struct particle *ret = info->from_conf(
        pair.value, parent_font, left, right, on_click_template);

    const struct yml_node *deco_node = yml_get_value(pair.value, "deco");

    if (deco_node != NULL)
        ret->deco = deco_from_config(deco_node);

    return ret;
}

struct bar *
conf_to_bar(const struct yml_node *bar)
{
    if (!conf_verify_bar(bar))
        return NULL;

    struct bar_config conf = {0};

    /*
     * Required attributes
     */

    const struct yml_node *height = yml_get_value(bar, "height");
    conf.height = yml_value_as_int(height);

    const struct yml_node *location = yml_get_value(bar, "location");
    conf.location = strcmp(yml_value_as_string(location), "top") == 0
        ? BAR_TOP : BAR_BOTTOM;

    const struct yml_node *background = yml_get_value(bar, "background");
    conf.background = conf_to_color(background);

    /*
     * Optional attributes
     */

    const struct yml_node *spacing = yml_get_value(bar, "spacing");
    if (spacing != NULL)
        conf.left_spacing = conf.right_spacing = yml_value_as_int(spacing);

    const struct yml_node *left_spacing = yml_get_value(bar, "left-spacing");
    if (left_spacing != NULL)
        conf.left_spacing = yml_value_as_int(left_spacing);

    const struct yml_node *right_spacing = yml_get_value(bar, "right-spacing");
    if (right_spacing != NULL)
        conf.right_spacing = yml_value_as_int(right_spacing);

    const struct yml_node *margin = yml_get_value(bar, "margin");
    if (margin != NULL)
        conf.left_margin = conf.right_margin = yml_value_as_int(margin);

    const struct yml_node *left_margin = yml_get_value(bar, "left-margin");
    if (left_margin != NULL)
        conf.left_margin = yml_value_as_int(left_margin);

    const struct yml_node *right_margin = yml_get_value(bar, "right-margin");
    if (right_margin != NULL)
        conf.right_margin = yml_value_as_int(right_margin);

    const struct yml_node *border = yml_get_value(bar, "border");
    if (border != NULL) {
        const struct yml_node *width = yml_get_value(border, "width");
        const struct yml_node *color = yml_get_value(border, "color");

        if (width != NULL)
            conf.border.width = yml_value_as_int(width);

        if (color != NULL)
            conf.border.color = conf_to_color(color);
    }

    /* Create a default font */
    struct font *font = font_new("sans");

    const struct yml_node *font_node = yml_get_value(bar, "font");
    if (font_node != NULL) {
        font_destroy(font);
        font = conf_to_font(font_node);
    }

    const struct yml_node *left = yml_get_value(bar, "left");
    const struct yml_node *center = yml_get_value(bar, "center");
    const struct yml_node *right = yml_get_value(bar, "right");

    for (size_t i = 0; i < 3; i++) {
        const struct yml_node *node = i == 0 ? left : i == 1 ? center : right;

        if (node != NULL) {
            const size_t count = yml_list_length(node);
            struct module **mods = calloc(count, sizeof(*mods));

            size_t idx = 0;
            for (struct yml_list_iter it = yml_list_iter(node);
                 it.node != NULL;
                 yml_list_next(&it), idx++)
            {
                struct yml_dict_iter m = yml_dict_iter(it.node);
                const char *mod_name = yml_value_as_string(m.key);

                const struct module_info *info = plugin_load_module(mod_name);
                mods[idx] = info->from_conf(m.value, font);
            }

            if (i == 0) {
                conf.left.mods = mods;
                conf.left.count = count;
            } else if (i == 1) {
                conf.center.mods = mods;
                conf.center.count = count;
            } else {
                conf.right.mods = mods;
                conf.right.count = count;
            }
        }
    }

    struct bar *ret = bar_new(&conf);

    free(conf.left.mods);
    free(conf.center.mods);
    free(conf.right.mods);
    font_destroy(font);

    return ret;
}
