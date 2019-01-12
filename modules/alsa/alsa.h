#pragma once

#include "../../font.h"
#include "../../module.h"
#include "../../particle.h"
#include "../../yml.h"

struct module *module_alsa_from_config(
    const struct yml_node *node, const struct font *parent_font);
