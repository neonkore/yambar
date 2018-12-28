#pragma once

#include <stddef.h>

#include <stdbool.h>

enum tag_realtime_unit {
    TAG_REALTIME_NONE,
    TAG_REALTIME_SECONDS
};

struct module;

struct tag {
    void *private;
    struct module *owner;

    void (*destroy)(struct tag *tag);
    const char *(*name)(const struct tag *tag);
    const char *(*as_string)(const struct tag *tag);
    long (*as_int)(const struct tag *tag);
    bool (*as_bool)(const struct tag *tag);
    double (*as_float)(const struct tag *tag);

    long (*min)(const struct tag *tag);
    long (*max)(const struct tag *tag);
    enum tag_realtime_unit (*realtime)(const struct tag *tag);
};

struct tag_set {
    struct tag **tags;
    size_t count;
};

struct tag *tag_new_int(struct module *owner, const char *name, long value);
struct tag *tag_new_int_range(
    struct module *owner, const char *name, long value, long min, long max);
struct tag *tag_new_int_realtime(
    struct module *owner, const char *name, long value, long min,
    long max, enum tag_realtime_unit unit);
struct tag *tag_new_bool(struct module *owner, const char *name, bool value);
struct tag *tag_new_float(struct module *owner, const char *name, double value);
struct tag *tag_new_string(
    struct module *owner, const char *name, const char *value);

const struct tag *tag_for_name(const struct tag_set *set, const char *name);
void tag_set_destroy(struct tag_set *set);
