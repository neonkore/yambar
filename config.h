#pragma once

#include <fcft/fcft.h>
#include "yml.h"
#include "bar/bar.h"

struct bar;
struct particle;

bool conf_verify_bar(const struct yml_node *bar);
struct bar *conf_to_bar(const struct yml_node *bar, enum bar_backend backend);

/*
 * Utility functions, for e.g. modules
 */

pixman_color_t conf_to_color(const struct yml_node *node);
struct font *conf_to_font(const struct yml_node *node);

struct conf_inherit {
    const struct font *font;
    pixman_color_t foreground;
};

struct particle *conf_to_particle(
    const struct yml_node *node, struct conf_inherit inherited);
struct deco *conf_to_deco(const struct yml_node *node);
