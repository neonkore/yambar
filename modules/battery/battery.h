#pragma once

#include "../../module.h"
#include "../../particle.h"

struct module *module_battery(
    const char *battery, struct particle *label, int poll_interval_secs);
