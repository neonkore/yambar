#pragma once

#include <stddef.h>

struct tag {
    void *private;

    void (*destroy)(struct tag *tag);
    const char *(*name)(const struct tag *tag);
    const char *(*value)(const struct tag *tag);
};

struct tag_set {
    struct tag **tags;
    size_t count;
};

struct tag *tag_new_int(const char *name, long value);
struct tag *tag_new_float(const char *name, double value);
struct tag *tag_new_string(const char *name, const char *value);

const struct tag *tag_for_name(const struct tag_set *set, const char *name);
void tag_set_destroy(struct tag_set *set);
