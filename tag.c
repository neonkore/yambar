#include "tag.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define LOG_MODULE "tag"
#define LOG_ENABLE_DBG 1
#include "log.h"
#include "module.h"

struct private {
    char *name;
    union {
        struct {
            long value;
            long min;
            long max;
            enum tag_realtime_unit realtime_unit;
        } value_as_int;
        bool value_as_bool;
        double value_as_float;
        char *value_as_string;
    };
};

static const char *
tag_name(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->name;
}

static long
unimpl_min_max(const struct tag *tag)
{
    return 0;
}

static enum tag_realtime_unit
no_realtime(const struct tag *tag)
{
    return TAG_REALTIME_NONE;
}

static bool
unimpl_refresh_in(const struct tag *tag, long units)
{
    return false;
}

static void
destroy_int_and_float(struct tag *tag)
{
    struct private *priv = tag->private;
    free(priv->name);
    free(priv);
    free(tag);
}

static void
destroy_string(struct tag *tag)
{
    struct private *priv = tag->private;
    free(priv->value_as_string);
    destroy_int_and_float(tag);
}

static long
int_min(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.min;
}

static long
int_max(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.max;
}

static enum tag_realtime_unit
int_realtime(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.realtime_unit;
}

static const char *
int_as_string(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%ld", priv->value_as_int.value);
    return as_string;
}

static long
int_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.value;
}

static bool
int_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.value;
}

static double
int_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_int.value;
}

static bool
int_refresh_in(const struct tag *tag, long units)
{
    const struct private *priv = tag->private;
    if (priv->value_as_int.realtime_unit == TAG_REALTIME_NONE)
        return false;

    if (tag->owner == NULL || tag->owner->refresh_in == NULL)
        return false;

    assert(priv->value_as_int.realtime_unit == TAG_REALTIME_SECS ||
           priv->value_as_int.realtime_unit == TAG_REALTIME_MSECS);

    long milli_seconds = units;
    if (priv->value_as_int.realtime_unit == TAG_REALTIME_SECS)
        milli_seconds *= 1000;

    return tag->owner->refresh_in(tag->owner, milli_seconds);
}

static const char *
bool_as_string(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool ? "true" : "false";
}

static long
bool_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static bool
bool_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static double
bool_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_bool;
}

static const char *
float_as_string(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%.2f", priv->value_as_float);
    return as_string;
}

static long
float_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static bool
float_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static double
float_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_float;
}

static const char *
string_as_string(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_string;
}

static long
string_as_int(const struct tag *tag)
{
    const struct private *priv = tag->private;

    long value;
    int matches = sscanf(priv->value_as_string, "%ld", &value);
    return matches == 1 ? value : 0;
}

static bool
string_as_bool(const struct tag *tag)
{
    const struct private *priv = tag->private;

    uint8_t value;
    int matches = sscanf(priv->value_as_string, "%hhu", &value);
    return matches == 1 ? value : 0;
}

static double
string_as_float(const struct tag *tag)
{
    const struct private *priv = tag->private;

    double value;
    int matches = sscanf(priv->value_as_string, "%lf", &value);
    return matches == 1 ? value : 0;
}

struct tag *
tag_new_int(struct module *owner, const char *name, long value)
{
    return tag_new_int_range(owner, name, value, value, value);
}

struct tag *
tag_new_int_range(struct module *owner, const char *name, long value,
                  long min, long max)
{
    return tag_new_int_realtime(owner, name, value, min, max, TAG_REALTIME_NONE);
}

struct tag *
tag_new_int_realtime(struct module *owner, const char *name, long value,
                     long min, long max, enum tag_realtime_unit unit)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_int.value = value;
    priv->value_as_int.min = min;
    priv->value_as_int.max = max;
    priv->value_as_int.realtime_unit = unit;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->owner = owner;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->min = &int_min;
    tag->max = &int_max;
    tag->realtime = &int_realtime;
    tag->refresh_in = &int_refresh_in;
    tag->as_string = &int_as_string;
    tag->as_int = &int_as_int;
    tag->as_bool = &int_as_bool;
    tag->as_float = &int_as_float;
    return tag;
}

struct tag *
tag_new_bool(struct module *owner, const char *name, bool value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_bool = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->owner = owner;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->min = &unimpl_min_max;
    tag->max = &unimpl_min_max;
    tag->realtime = &no_realtime;
    tag->refresh_in = &unimpl_refresh_in;
    tag->as_string = &bool_as_string;
    tag->as_int = &bool_as_int;
    tag->as_bool = &bool_as_bool;
    tag->as_float = &bool_as_float;
    return tag;
}

struct tag *
tag_new_float(struct module *owner, const char *name, double value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_float = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->owner = owner;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->min = &unimpl_min_max;
    tag->max = &unimpl_min_max;
    tag->realtime = &no_realtime;
    tag->refresh_in = &unimpl_refresh_in;
    tag->as_string = &float_as_string;
    tag->as_int = &float_as_int;
    tag->as_bool = &float_as_bool;
    tag->as_float = &float_as_float;
    return tag;
}

struct tag *
tag_new_string(struct module *owner, const char *name, const char *value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_string = value != NULL ? strdup(value) : strdup("");

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->owner = owner;
    tag->destroy = &destroy_string;
    tag->name = &tag_name;
    tag->min = &unimpl_min_max;
    tag->max = &unimpl_min_max;
    tag->realtime = &no_realtime;
    tag->refresh_in = &unimpl_refresh_in;
    tag->as_string = &string_as_string;
    tag->as_int = &string_as_int;
    tag->as_bool = &string_as_bool;
    tag->as_float = &string_as_float;
    return tag;
}

const struct tag *
tag_for_name(const struct tag_set *set, const char *name)
{
    if (set == NULL)
        return NULL;

    for (size_t i = 0; i < set->count; i++) {
        const struct tag *tag = set->tags[i];
        if (strcmp(tag->name(tag), name) == 0)
            return tag;
    }

    return NULL;
}

void
tag_set_destroy(struct tag_set *set)
{
    for (size_t i = 0; i < set->count; i++)
        set->tags[i]->destroy(set->tags[i]);

    set->tags = NULL;
    set->count = 0;
}

struct sbuf {
    char *s;
    size_t size;
    size_t len;
};

static void
sbuf_append_at_most(struct sbuf *s1, const char *s2, size_t n)
{
    if (s1->len + n >= s1->size) {
        size_t required_size = s1->len + n + 1;
        s1->size = 2 * required_size;

        s1->s = realloc(s1->s, s1->size);
        //s1->s[s1->len] = '\0';
    }

    memcpy(&s1->s[s1->len], s2, n);
    s1->len += n;
    s1->s[s1->len] = '\0';
}

static void
sbuf_append(struct sbuf *s1, const char *s2)
{
    sbuf_append_at_most(s1, s2, strlen(s2));
}

char *
tags_expand_template(const char *template, const struct tag_set *tags)
{
    if (template == NULL)
        return NULL;

    struct sbuf formatted = {0};
    while (true) {
        /* Find next tag opening '{' */
        const char *begin = strchr(template, '{');

        if (begin == NULL) {
            /* No more tags, copy remaining characters */
            sbuf_append(&formatted, template);
            break;
        }

        /* Find closing '}' */
        const char *end = strchr(begin, '}');
        if (end == NULL) {
            /* Wasn't actually a tag, copy as-is instead */
            sbuf_append_at_most(&formatted, template, begin - template + 1);
            template = begin + 1;
            continue;
        }

        /* Extract tag name + argument*/
        char tag_name_and_arg[end - begin];
        strncpy(tag_name_and_arg, begin + 1, end - begin - 1);
        tag_name_and_arg[end - begin - 1] = '\0';

        const char *tag_name = NULL;
        const char *tag_arg = NULL;

        {
            char *saveptr;
            tag_name = strtok_r(tag_name_and_arg, ":", &saveptr);
            tag_arg = strtok_r(NULL, ":", &saveptr);
        }

        /* Lookup tag */
        const struct tag *tag = tag_for_name(tags, tag_name);
        if (tag == NULL) {
            /* No such tag, copy as-is instead */
            sbuf_append_at_most(&formatted, template, begin - template + 1);
            template = begin + 1;
            continue;
        }

        /* Copy characters preceeding the tag (name) */
        sbuf_append_at_most(&formatted, template, begin - template);

        /* Copy tag value */
        const char *value = tag->as_string(tag);
        sbuf_append(&formatted, value);

        /* Skip past tag name + closing '}' */
        template = end + 1;
    }

    return formatted.s;
}
