#pragma once

#include "font.h"
#include "yml.h"

struct bar;
struct particle;

bool conf_verify_bar(const struct yml_node *bar);
struct bar *conf_to_bar(const struct yml_node *bar);

/*
 * Utility functions, for e.g. modules
 */

struct rgba conf_to_color(const struct yml_node *node);
struct font *conf_to_font(const struct yml_node *node);

struct conf_inherit {
    const struct font *font;
    struct rgba foreground;
};

struct particle * conf_to_particle(
    const struct yml_node *node, struct conf_inherit inherited);
