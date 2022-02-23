#pragma once

#include <fcft/fcft.h>
#include "yml.h"
#include "bar/bar.h"
#include "font-shaping.h"

struct bar;
struct particle;

bool conf_verify_bar(const struct yml_node *bar);
struct bar *conf_to_bar(const struct yml_node *bar, enum bar_backend backend);

/*
 * Utility functions, for e.g. modules
 */

pixman_color_t conf_to_color(const struct yml_node *node);
struct fcft_font *conf_to_font(const struct yml_node *node);
enum font_shaping conf_to_font_shaping(const struct yml_node *node);

struct conf_inherit {
    const struct fcft_font *font;
    enum font_shaping font_shaping;
    pixman_color_t foreground;
};

struct particle *conf_to_particle(
    const struct yml_node *node, struct conf_inherit inherited);
struct deco *conf_to_deco(const struct yml_node *node);
