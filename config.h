#pragma once

#include "bar.h"
#include "yml.h"
#include "font.h"
#include "particle.h"

bool conf_verify_bar(const struct yml_node *bar);
struct bar *conf_to_bar(const struct yml_node *bar);

struct particle * conf_to_particle(
    const struct yml_node *node, const struct font *parent_font);
