#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <dlfcn.h>

#include "bar/bar.h"
#include "color.h"
#include "config-verify.h"
#include "module.h"
#include "plugin.h"

#define LOG_MODULE "config"
#define LOG_ENABLE_DBG 0
#include "log.h"

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

pixman_color_t
conf_to_color(const struct yml_node *node)
{
    const char *hex = yml_value_as_string(node);

    assert(hex != NULL);
    assert(strlen(hex) == 8);

    uint16_t red = hex_byte(&hex[0]);
    uint16_t green = hex_byte(&hex[2]);
    uint16_t blue = hex_byte(&hex[4]);
    uint16_t alpha = hex_byte(&hex[6]);

    alpha |= alpha << 8;
    int alpha_div = 0xffff / alpha;

    return (pixman_color_t){
        .red =   (red << 8 | red) / alpha_div,
        .green = (green << 8 | green) / alpha_div,
        .blue =  (blue << 8 | blue) / alpha_div,
        .alpha = alpha,
    };
}

struct font *
conf_to_font(const struct yml_node *node)
{
    return font_from_name(&(const char *){yml_value_as_string(node)}, 1, NULL);
}

struct deco *
conf_to_deco(const struct yml_node *node)
{
    struct yml_dict_iter it = yml_dict_iter(node);
    const struct yml_node *deco_type = it.key;
    const struct yml_node *deco_data = it.value;

    const char *type = yml_value_as_string(deco_type);
    const struct deco_iface *iface = plugin_load_deco(type);

    assert(iface != NULL);
    return iface->from_conf(deco_data);
}

static struct particle *
particle_simple_list_from_config(const struct yml_node *node,
                                 struct conf_inherit inherited)
{
    size_t count = yml_list_length(node);
    struct particle *parts[count];

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = conf_to_particle(it.node, inherited);
    }

    /* Lazy-loaded function pointer to particle_list_new() */
    static struct particle *(*particle_list_new)(
        struct particle *common,
        struct particle *particles[], size_t count,
        int left_spacing, int right_spacing) = NULL;

    if (particle_list_new == NULL) {
        const struct plugin *plug = plugin_load("list", PLUGIN_PARTICLE);

        particle_list_new = dlsym(plug->lib, "particle_list_new");
        assert(particle_list_new != NULL);
    }

    struct particle *common = particle_common_new(
        0, 0, NULL, font_clone(inherited.font), inherited.foreground, NULL);

    return particle_list_new(common, parts, count, 0, 2);
}

struct particle *
conf_to_particle(const struct yml_node *node, struct conf_inherit inherited)
{
    if (yml_is_list(node))
        return particle_simple_list_from_config(node, inherited);

    struct yml_dict_iter pair = yml_dict_iter(node);
    const char *type = yml_value_as_string(pair.key);

    const struct yml_node *margin = yml_get_value(pair.value, "margin");
    const struct yml_node *left_margin = yml_get_value(pair.value, "left-margin");
    const struct yml_node *right_margin = yml_get_value(pair.value, "right-margin");
    const struct yml_node *on_click = yml_get_value(pair.value, "on-click");
    const struct yml_node *font_node = yml_get_value(pair.value, "font");
    const struct yml_node *foreground_node = yml_get_value(pair.value, "foreground");
    const struct yml_node *deco_node = yml_get_value(pair.value, "deco");

    int left = margin != NULL ? yml_value_as_int(margin) :
        left_margin != NULL ? yml_value_as_int(left_margin) : 0;
    int right = margin != NULL ? yml_value_as_int(margin) :
        right_margin != NULL ? yml_value_as_int(right_margin) : 0;

    const char *on_click_template
        = on_click != NULL ? yml_value_as_string(on_click) : NULL;
    struct deco *deco = deco_node != NULL ? conf_to_deco(deco_node) : NULL;

    /*
     * Font and foreground are inheritable attributes. Each particle
     * may specify its own font/foreground values, which will then be
     * used by itself, and all its sub-particles. If *not* specified,
     * we inherit the values from our parent. Note that since
     * particles actually *use* the font/foreground values, we must
     * clone the font, since each particle takes ownership of its own
     * font.
     */
    struct font *font = font_node != NULL
        ? conf_to_font(font_node) : font_clone(inherited.font);
    pixman_color_t foreground = foreground_node != NULL
        ? conf_to_color(foreground_node) : inherited.foreground;

    /* Instantiate base/common particle */
    struct particle *common = particle_common_new(
        left, right, on_click_template, font, foreground, deco);

    const struct particle_iface *iface = plugin_load_particle(type);

    assert(iface != NULL);
    return iface->from_conf(pair.value, common);
}

struct bar *
conf_to_bar(const struct yml_node *bar, enum bar_backend backend)
{
    if (!conf_verify_bar(bar))
        return NULL;

    struct bar_config conf = {
        .backend = backend,
    };

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

    const struct yml_node *monitor = yml_get_value(bar, "monitor");
    if (monitor != NULL)
        conf.monitor = yml_value_as_string(monitor);

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
        const struct yml_node *margin = yml_get_value(border, "margin");
        const struct yml_node *left_margin = yml_get_value(border, "left-margin");
        const struct yml_node *right_margin = yml_get_value(border, "right-margin");
        const struct yml_node *top_margin = yml_get_value(border, "top-margin");
        const struct yml_node *bottom_margin = yml_get_value(border, "bottom-margin");

        if (width != NULL)
            conf.border.width = yml_value_as_int(width);

        if (color != NULL)
            conf.border.color = conf_to_color(color);

        if (margin != NULL)
            conf.border.left_margin =
                conf.border.right_margin =
                conf.border.top_margin =
                conf.border.bottom_margin = yml_value_as_int(margin);

        if (left_margin != NULL)
            conf.border.left_margin = yml_value_as_int(left_margin);
        if (right_margin != NULL)
            conf.border.right_margin = yml_value_as_int(right_margin);
        if (top_margin != NULL)
            conf.border.top_margin = yml_value_as_int(top_margin);
        if (bottom_margin != NULL)
            conf.border.bottom_margin = yml_value_as_int(bottom_margin);
    }

    /*
     * Create a default font and foreground
     *
     * These aren't used by the bar itself, but passed down to modules
     * and particles. This allows us to specify a default font and
     * foreground color at top-level.
     */
    struct font *font = font_from_name(&(const char *){"sans"}, 1, NULL);
    pixman_color_t foreground = {0xffff, 0xffff, 0xffff, 0xffff}; /* White */

    const struct yml_node *font_node = yml_get_value(bar, "font");
    if (font_node != NULL) {
        font_destroy(font);
        font = conf_to_font(font_node);
    }

    const struct yml_node *foreground_node = yml_get_value(bar, "foreground");
    if (foreground_node != NULL)
        foreground = conf_to_color(foreground_node);

    struct conf_inherit inherited = {
        .font = font,
        .foreground = foreground,
    };

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

                /*
                 * These aren't used by the modules, but passed down
                 * to particles. This allows us to specify a default
                 * font and foreground for each module, and having it
                 * applied to all its particles.
                 */
                const struct yml_node *mod_font = yml_get_value(m.value, "font");
                const struct yml_node *mod_foreground = yml_get_value(
                    m.value, "foreground");

                struct conf_inherit mod_inherit = {
                    .font = mod_font != NULL
                        ? conf_to_font(mod_font) : inherited.font,
                    .foreground = mod_foreground != NULL
                        ? conf_to_color(mod_foreground) : inherited.foreground,
                };

                const struct module_iface *iface = plugin_load_module(mod_name);
                mods[idx] = iface->from_conf(m.value, mod_inherit);
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
