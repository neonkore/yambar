#pragma once

#include "../module.h"
#include "../particle.h"

struct module *module_clock(
    struct particle *label, const char *date_format, const char *time_format);
