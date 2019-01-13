#pragma once

#include "bar.h"
#include "font.h"
#include "particle.h"
#include "yml.h"

bool conf_verify_bar(const struct yml_node *bar);
struct bar *conf_to_bar(const struct yml_node *bar);

/*
 * Utility functions, for e.g. modules
 */

struct rgba conf_to_color(const struct yml_node *node);
struct font *conf_to_font(const struct yml_node *node);

struct particle * conf_to_particle(
    const struct yml_node *node, const struct font *parent_font);
