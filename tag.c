#include "tag.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include<errno.h>

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

static enum tag_type
bool_type(const struct tag *tag)
{
    return TAG_TYPE_BOOL;
}

static enum tag_type
int_type(const struct tag *tag)
{
    return TAG_TYPE_INT;
}

static enum tag_type
float_type(const struct tag *tag)
{
    return TAG_TYPE_FLOAT;
}

static enum tag_type
string_type(const struct tag *tag)
{
    return TAG_TYPE_STRING;
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
    tag->type = &int_type;
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
    tag->type = &bool_type;
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
    tag->type = &float_type;
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
    tag->type = &string_type;
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

// stores the number in "*value" on success
static bool
is_number(const char *str, int *value)
{
    errno = 0;

    char *end;
    int v = strtol(str, &end, 10);
    if (errno != 0 || *end != '\0')
        return false;

    *value = v;
    return true;
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

        static const size_t MAX_TAG_ARGS = 4;
        const char *tag_name = NULL;
        const char *tag_args[MAX_TAG_ARGS];
        memset(tag_args, 0, sizeof(tag_args));

        {
            char *saveptr;
            tag_name = strtok_r(tag_name_and_arg, ":", &saveptr);

            for (size_t i = 0; i < MAX_TAG_ARGS; i++) {
                const char *arg = strtok_r(NULL, ":", &saveptr);
                if (arg == NULL)
                    break;
                tag_args[i] = arg;
            }
        }

        /* Lookup tag */
        const struct tag *tag = NULL;

        if (tag_name == NULL || (tag = tag_for_name(tags, tag_name)) == NULL) {
            /* No such tag, copy as-is instead */
            sbuf_append_at_most(&formatted, template, begin - template + 1);
            template = begin + 1;
            continue;
        }

        /* Copy characters preceding the tag (name) */
        sbuf_append_at_most(&formatted, template, begin - template);

        /* Parse arguments */
        enum {
            FMT_DEFAULT,
            FMT_HEX,
            FMT_OCT,
            FMT_PERCENT,
            FMT_KBYTE,
            FMT_MBYTE,
            FMT_GBYTE,
            FMT_KIBYTE,
            FMT_MIBYTE,
            FMT_GIBYTE,
        } format = FMT_DEFAULT;

        enum {
            VALUE_VALUE,
            VALUE_MIN,
            VALUE_MAX,
            VALUE_UNIT,
        } kind = VALUE_VALUE;

        int digits = 0;
        int decimals = 2;
        bool zero_pad = false;
        char *point = NULL;

        for (size_t i = 0; i < MAX_TAG_ARGS; i++) {
            if (tag_args[i] == NULL)
                break;
            else if (strcmp(tag_args[i], "hex") == 0)
                format = FMT_HEX;
            else if (strcmp(tag_args[i], "oct") == 0)
                format = FMT_OCT;
            else if (strcmp(tag_args[i], "%") == 0)
                format = FMT_PERCENT;
            else if (strcmp(tag_args[i], "kb") == 0)
                format = FMT_KBYTE;
            else if (strcmp(tag_args[i], "mb") == 0)
                format = FMT_MBYTE;
            else if (strcmp(tag_args[i], "gb") == 0)
                format = FMT_GBYTE;
            else if (strcmp(tag_args[i], "kib") == 0)
                format = FMT_KIBYTE;
            else if (strcmp(tag_args[i], "mib") == 0)
                format = FMT_MIBYTE;
            else if (strcmp(tag_args[i], "gib") == 0)
                format = FMT_GIBYTE;
            else if (strcmp(tag_args[i], "min") == 0)
                kind = VALUE_MIN;
            else if (strcmp(tag_args[i], "max") == 0)
                kind = VALUE_MAX;
            else if (strcmp(tag_args[i], "unit") == 0)
                kind = VALUE_UNIT;
            else if (is_number(tag_args[i], &digits)) // i.e.: "{tag:3}"
                zero_pad = tag_args[i][0] == '0';
            else if ((point = strchr(tag_args[i], '.')) != NULL) {
                *point = '\0';

                const char *digits_str = tag_args[i];
                const char *decimals_str = point + 1;

                if (digits_str[0] != '\0') { // guards against i.e. "{tag:.3}"
                    if (!is_number(digits_str, &digits)) {
                        LOG_WARN(
                            "tag `%s`: invalid field width formatter. Ignoring...",
                            tag_name);
                    }
                }

                if (decimals_str[0] != '\0') { // guards against i.e. "{tag:3.}"
                    if (!is_number(decimals_str, &decimals)) {
                        LOG_WARN(
                            "tag `%s`: invalid decimals formatter. Ignoring...",
                            tag_name);
                    }
                }
                zero_pad = digits_str[0] == '0';
            }
            else
                LOG_WARN("invalid tag formatter: %s", tag_args[i]);
        }

        /* Copy tag value */
        switch (kind) {
        case VALUE_VALUE:
            switch (format) {
            case FMT_DEFAULT: {
                switch (tag->type(tag)) {
                case TAG_TYPE_FLOAT: {
                    const char* fmt = zero_pad ? "%0*.*f" : "%*.*f";
                    char str[24];
                    snprintf(str, sizeof(str), fmt, digits, decimals, tag->as_float(tag));
                    sbuf_append(&formatted, str);
                    break;
                }

                case TAG_TYPE_INT: {
                    const char* fmt = zero_pad ? "%0*ld" : "%*ld";
                    char str[24];
                    snprintf(str, sizeof(str), fmt, digits, tag->as_int(tag));
                    sbuf_append(&formatted, str);
                    break;
                }

                default:
                    sbuf_append(&formatted, tag->as_string(tag));
                    break;
                }

                break;
            }

            case FMT_HEX:
            case FMT_OCT: {
                const char* fmt = format == FMT_HEX ?
                    zero_pad ? "%0*lx" : "%*lx" :
                    zero_pad ? "%0*lo" : "%*lo";
                char str[24];
                snprintf(str, sizeof(str), fmt, digits, tag->as_int(tag));
                sbuf_append(&formatted, str);
                break;
            }

            case FMT_PERCENT: {
                const long min = tag->min(tag);
                const long max = tag->max(tag);
                const long cur = tag->as_int(tag);

                const char* fmt = zero_pad ? "%0*lu" : "%*lu";
                char str[4];
                snprintf(str, sizeof(str), fmt, digits, (cur - min) * 100 / (max - min));
                sbuf_append(&formatted, str);
                break;
            }

            case FMT_KBYTE:
            case FMT_MBYTE:
            case FMT_GBYTE:
            case FMT_KIBYTE:
            case FMT_MIBYTE:
            case FMT_GIBYTE: {
                const long divider =
                    format == FMT_KBYTE ? 1000 :
                    format == FMT_MBYTE ? 1000 * 1000 :
                    format == FMT_GBYTE ? 1000 * 1000 * 1000 :
                    format == FMT_KIBYTE ? 1024 :
                    format == FMT_MIBYTE ? 1024 * 1024 :
                    format == FMT_GIBYTE ? 1024 * 1024 * 1024 :
                    1;

                char str[24];
                if (tag->type(tag) == TAG_TYPE_FLOAT) {
                    const char* fmt = zero_pad ? "%0*.*f" : "%*.*f";
                    snprintf(str, sizeof(str), fmt, digits, decimals, tag->as_float(tag) / (double)divider);
                } else {
                    const char* fmt = zero_pad ? "%0*lu" : "%*lu";
                    snprintf(str, sizeof(str), fmt, digits, tag->as_int(tag) / divider);
                }
                sbuf_append(&formatted, str);
                break;
            }
            }
            break;

        case VALUE_MIN:
        case VALUE_MAX: {
            const long min = tag->min(tag);
            const long max = tag->max(tag);
            long value = kind == VALUE_MIN ? min : max;

            const char *fmt = NULL;
            switch (format) {
            case FMT_DEFAULT: fmt = zero_pad ? "%0*ld" : "%*ld"; break;
            case FMT_HEX:     fmt = zero_pad ? "%0*lx" : "%*lx"; break;
            case FMT_OCT:     fmt = zero_pad ? "%0*lo" : "%*lo"; break;
            case FMT_PERCENT:
                value = (value - min) * 100 / (max - min);
                fmt = zero_pad ? "%0*lu" : "%*lu";
                break;

            case FMT_KBYTE:
            case FMT_MBYTE:
            case FMT_GBYTE:
            case FMT_KIBYTE:
            case FMT_MIBYTE:
            case FMT_GIBYTE: {
                const long divider =
                    format == FMT_KBYTE ? 1024 :
                    format == FMT_MBYTE ? 1024 * 1024 :
                    format == FMT_GBYTE ? 1024 * 1024 * 1024 :
                    format == FMT_KIBYTE ? 1000 :
                    format == FMT_MIBYTE ? 1000 * 1000 :
                    format == FMT_GIBYTE ? 1000 * 1000 * 1000 :
                    1;
                value /= divider;
                fmt = zero_pad ? "%0*lu" : "%*lu";
                break;
            }
            }

            assert(fmt != NULL);

            char str[24];
            snprintf(str, sizeof(str), fmt, digits, value);
            sbuf_append(&formatted, str);
            break;
        }

        case VALUE_UNIT: {
            const char *value = NULL;

            switch (tag->realtime(tag)) {
            case TAG_REALTIME_NONE:  value = ""; break;
            case TAG_REALTIME_SECS:  value = "s"; break;
            case TAG_REALTIME_MSECS: value = "ms"; break;
            }

            sbuf_append(&formatted, value);
            break;
        }
        }

        /* Skip past tag name + closing '}' */
        template = end + 1;
    }

    return formatted.s;
}

void
tags_expand_templates(char *expanded[], const char *template[], size_t nmemb,
                      const struct tag_set *tags)
{
    for (size_t i = 0; i < nmemb; i++)
        expanded[i] = tags_expand_template(template[i], tags);
}
