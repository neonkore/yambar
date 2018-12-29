#pragma once

#include "../module.h"
#include "../particle.h"

/* Maps workspace name to a content particle. */
struct i3_workspaces {
    const char *name;
    struct particle *content;
};

struct module *module_i3(
    struct i3_workspaces workspaces[], size_t workspace_count,
    int left_spacing, int right_spacing);
