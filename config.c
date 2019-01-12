#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "color.h"

#include "decoration.h"
#include "decorations/background.h"
#include "decorations/stack.h"
#include "decorations/underline.h"

#include "particle.h"
#include "particles/empty.h"
#include "particles/list.h"
#include "particles/map.h"
#include "particles/progress-bar.h"
#include "particles/ramp.h"
#include "particles/string.h"

#include "module.h"
#include "modules/alsa/alsa.h"
#include "modules/backlight/backlight.h"
#include "modules/battery/battery.h"
#include "modules/clock/clock.h"
#include "modules/i3/i3.h"
#include "modules/label/label.h"
#include "modules/mpd/mpd.h"
#include "modules/network/network.h"
#include "modules/removables/removables.h"
#include "modules/xkb/xkb.h"
#include "modules/xwindow/xwindow.h"

#include "config-verify.h"

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

static struct rgba
color_from_hexstr(const char *hex)
{
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

static struct font *
font_from_config(const struct yml_node *node)
{
    const struct yml_node *family = yml_get_value(node, "family");
    return font_new(family != NULL ? yml_value_as_string(family) : "monospace");
}

static struct deco *
deco_background_from_config(const struct yml_node *node)
{
    const struct yml_node *color = yml_get_value(node, "color");
    return deco_background(color_from_hexstr(yml_value_as_string(color)));
}

static struct deco *
deco_underline_from_config(const struct yml_node *node)
{
    const struct yml_node *size = yml_get_value(node, "size");
    const struct yml_node *color = yml_get_value(node, "color");

    return deco_underline(
        yml_value_as_int(size),
        color_from_hexstr(yml_value_as_string(color)));
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
particle_empty_from_config(const struct yml_node *node,
                           const struct font *parent_font,
                           int left_margin, int right_margin,
                           const char *on_click_template)
{
    return particle_empty_new(left_margin, right_margin, on_click_template);
}

static struct particle *
particle_string_from_config(const struct yml_node *node,
                            const struct font *parent_font,
                            int left_margin, int right_margin,
                            const char *on_click_template)
{
    const struct yml_node *text = yml_get_value(node, "text");
    const struct yml_node *max = yml_get_value(node, "max");
    const struct yml_node *font = yml_get_value(node, "font");
    const struct yml_node *foreground = yml_get_value(node, "foreground");

    struct rgba fg_color = foreground != NULL
        ? color_from_hexstr(yml_value_as_string(foreground)) :
        (struct rgba){1.0, 1.0, 1.0, 1.0};

    return particle_string_new(
        yml_value_as_string(text),
        max != NULL ? yml_value_as_int(max) : 0,
        font != NULL ? font_from_config(font) : font_clone(parent_font),
        fg_color, left_margin, right_margin, on_click_template);
}

static struct particle *
particle_list_from_config(const struct yml_node *node,
                          const struct font *parent_font,
                          int left_margin, int right_margin,
                          const char *on_click_template)
{
    const struct yml_node *items = yml_get_value(node, "items");

    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *_left_spacing = yml_get_value(node, "left_spacing");
    const struct yml_node *_right_spacing = yml_get_value(node, "right_spacing");

    int left_spacing = spacing != NULL ? yml_value_as_int(spacing) :
        _left_spacing != NULL ? yml_value_as_int(_left_spacing) : 0;
    int right_spacing = spacing != NULL ? yml_value_as_int(spacing) :
        _right_spacing != NULL ? yml_value_as_int(_right_spacing) : 2;

    size_t count = yml_list_length(items);
    struct particle *parts[count];

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(items);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = conf_to_particle(it.node, parent_font);
    }

    return particle_list_new(
        parts, count, left_spacing, right_spacing, left_margin, right_margin,
        on_click_template);
}

static struct particle *
particle_map_from_config(const struct yml_node *node,
                         const struct font *parent_font,
                         int left_margin, int right_margin,
                         const char *on_click_template)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *values = yml_get_value(node, "values");
    const struct yml_node *def = yml_get_value(node, "default");

    struct particle_map particle_map[yml_dict_length(values)];

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(values);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        particle_map[idx].tag_value = yml_value_as_string(it.key);
        particle_map[idx].particle = conf_to_particle(it.value, parent_font);
    }

    struct particle *default_particle = def != NULL
        ? conf_to_particle(def, parent_font)
        : NULL;

    return particle_map_new(
        yml_value_as_string(tag), particle_map, yml_dict_length(values),
        default_particle, left_margin, right_margin, on_click_template);
}

static struct particle *
particle_ramp_from_config(const struct yml_node *node,
                          const struct font *parent_font,
                          int left_margin, int right_margin,
                          const char *on_click_template)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *items = yml_get_value(node, "items");

    size_t count = yml_list_length(items);
    struct particle *parts[count];

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(items);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = conf_to_particle(it.node, parent_font);
    }

    return particle_ramp_new(
        yml_value_as_string(tag), parts, count, left_margin, right_margin,
        on_click_template);
}

static struct particle *
particle_progress_bar_from_config(const struct yml_node *node,
                                  const struct font *parent_font,
                                  int left_margin, int right_margin,
                                  const char *on_click_template)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *length = yml_get_value(node, "length");
    const struct yml_node *start = yml_get_value(node, "start");
    const struct yml_node *end = yml_get_value(node, "end");
    const struct yml_node *fill = yml_get_value(node, "fill");
    const struct yml_node *empty = yml_get_value(node, "empty");
    const struct yml_node *indicator = yml_get_value(node, "indicator");

    return particle_progress_bar_new(
        yml_value_as_string(tag),
        yml_value_as_int(length),
        conf_to_particle(start, parent_font),
        conf_to_particle(end, parent_font),
        conf_to_particle(fill, parent_font),
        conf_to_particle(empty, parent_font),
        conf_to_particle(indicator, parent_font),
        left_margin, right_margin, on_click_template);
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
    const struct yml_node *left_margin = yml_get_value(pair.value, "left_margin");
    const struct yml_node *right_margin = yml_get_value(pair.value, "right_margin");
    const struct yml_node *on_click = yml_get_value(pair.value, "on_click");

    int left = margin != NULL ? yml_value_as_int(margin) :
        left_margin != NULL ? yml_value_as_int(left_margin) : 0;
    int right = margin != NULL ? yml_value_as_int(margin) :
        right_margin != NULL ? yml_value_as_int(right_margin) : 0;

    const char *on_click_template
        = on_click != NULL ? yml_value_as_string(on_click) : NULL;

    struct particle *ret = NULL;
    if (strcmp(type, "empty") == 0)
        ret = particle_empty_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else if (strcmp(type, "string") == 0)
        ret = particle_string_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else if (strcmp(type, "list") == 0)
        ret = particle_list_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else if (strcmp(type, "map") == 0)
        ret = particle_map_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else if (strcmp(type, "ramp") == 0)
        ret = particle_ramp_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else if (strcmp(type, "progress-bar") == 0)
        ret = particle_progress_bar_from_config(
            pair.value, parent_font, left, right, on_click_template);
    else
        assert(false);

    const struct yml_node *deco_node = yml_get_value(pair.value, "deco");

    if (deco_node != NULL)
        ret->deco = deco_from_config(deco_node);

    return ret;
}

static struct module *
module_label_from_config(const struct yml_node *node, const struct font *parent_font)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return module_label(conf_to_particle(c, parent_font));
}

static struct module *
module_xwindow_from_config(const struct yml_node *node, const struct font *parent_font)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return module_xwindow(conf_to_particle(c, parent_font));
}

static struct module *
module_xkb_from_config(const struct yml_node *node,
                       const struct font *parent_font)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return module_xkb(conf_to_particle(c, parent_font));
}

static struct module *
module_mpd_from_config(const struct yml_node *node,
                       const struct font *parent_font)
{
    const struct yml_node *host = yml_get_value(node, "host");
    const struct yml_node *port = yml_get_value(node, "port");
    const struct yml_node *c = yml_get_value(node, "content");

    return module_mpd(
        yml_value_as_string(host),
        port != NULL ? yml_value_as_int(port) : 0,
        conf_to_particle(c, parent_font));
}

static struct module *
module_network_from_config(const struct yml_node *node,
                           const struct font *parent_font)
{
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *content = yml_get_value(node, "content");

    return module_network(
        yml_value_as_string(name), conf_to_particle(content, parent_font));
}

static struct module *
module_removables_from_config(const struct yml_node *node,
                              const struct font *parent_font)
{
    const struct yml_node *content = yml_get_value(node, "content");
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing = yml_get_value(node, "left_spacing");
    const struct yml_node *right_spacing = yml_get_value(node, "right_spacing");

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    return module_removables(
        conf_to_particle(content, parent_font), left, right);
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
    conf.background = color_from_hexstr(yml_value_as_string(background));

    /*
     * Optional attributes
     */

    const struct yml_node *spacing = yml_get_value(bar, "spacing");
    if (spacing != NULL)
        conf.left_spacing = conf.right_spacing = yml_value_as_int(spacing);

    const struct yml_node *left_spacing = yml_get_value(bar, "left_spacing");
    if (left_spacing != NULL)
        conf.left_spacing = yml_value_as_int(left_spacing);

    const struct yml_node *right_spacing = yml_get_value(bar, "right_spacing");
    if (right_spacing != NULL)
        conf.right_spacing = yml_value_as_int(right_spacing);

    const struct yml_node *margin = yml_get_value(bar, "margin");
    if (margin != NULL)
        conf.left_margin = conf.right_margin = yml_value_as_int(margin);

    const struct yml_node *left_margin = yml_get_value(bar, "left_margin");
    if (left_margin != NULL)
        conf.left_margin = yml_value_as_int(left_margin);

    const struct yml_node *right_margin = yml_get_value(bar, "right_margin");
    if (right_margin != NULL)
        conf.right_margin = yml_value_as_int(right_margin);

    const struct yml_node *border = yml_get_value(bar, "border");
    if (border != NULL) {
        const struct yml_node *width = yml_get_value(border, "width");
        const struct yml_node *color = yml_get_value(border, "color");

        if (width != NULL)
            conf.border.width = yml_value_as_int(width);

        if (color != NULL)
            conf.border.color = color_from_hexstr(yml_value_as_string(color));
    }

    /* Create a default font */
    struct font *font = font_new("sans");

    const struct yml_node *font_node = yml_get_value(bar, "font");
    if (font_node != NULL) {
        font_destroy(font);
        font = font_from_config(font_node);
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

                if (strcmp(mod_name, "alsa") == 0)
                    mods[idx] = module_alsa.from_conf(m.value, font);
                else if (strcmp(mod_name, "backlight") == 0)
                    mods[idx] = module_backlight.from_conf(m.value, font);
                else if (strcmp(mod_name, "battery") == 0)
                    mods[idx] = module_battery.from_conf(m.value, font);
                else if (strcmp(mod_name, "clock") == 0)
                    mods[idx] = module_clock.from_conf(m.value, font);
                else if (strcmp(mod_name, "i3") == 0)
                    mods[idx] = module_i3.from_conf(m.value, font);
 
                else if (strcmp(mod_name, "label") == 0)
                    mods[idx] = module_label_from_config(m.value, font);
                else if (strcmp(mod_name, "xwindow") == 0)
                    mods[idx] = module_xwindow_from_config(m.value, font);
               else if (strcmp(mod_name, "xkb") == 0)
                    mods[idx] = module_xkb_from_config(m.value, font);
                else if (strcmp(mod_name, "mpd") == 0)
                    mods[idx] = module_mpd_from_config(m.value, font);
                else if (strcmp(mod_name, "network") == 0)
                    mods[idx] = module_network_from_config(m.value, font);
                else if (strcmp(mod_name, "removables") == 0)
                    mods[idx] = module_removables_from_config(m.value, font);
                else
                    assert(false);
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
