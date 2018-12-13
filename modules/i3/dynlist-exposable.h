#pragma once

#include <stddef.h>
#include "../../particle.h"

struct exposable *dynlist_exposable_new(
    struct exposable **exposables, size_t count, int left_spacing, int right_spacing);
