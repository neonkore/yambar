#include "tag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct private {
    char *name;
    union {
        long value_as_int;
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

static const char *
value_int(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%ld", priv->value_as_int);
    return as_string;
}

static const char *
value_float(const struct tag *tag)
{
    static char as_string[128];
    const struct private *priv = tag->private;

    snprintf(as_string, sizeof(as_string), "%.2f", priv->value_as_float);
    return as_string;
}

static const char *
value_string(const struct tag *tag)
{
    const struct private *priv = tag->private;
    return priv->value_as_string;
}

struct tag *
tag_new_int(const char *name, long value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_int = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->value = &value_int;
    return tag;
}

struct tag *
tag_new_float(const char *name, double value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_float = value;

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_int_and_float;
    tag->name = &tag_name;
    tag->value = &value_float;
    return tag;
}

struct tag *
tag_new_string(const char *name, const char *value)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->name = strdup(name);
    priv->value_as_string = strdup(value);

    struct tag *tag = malloc(sizeof(*tag));
    tag->private = priv;
    tag->destroy = &destroy_string;
    tag->name = &tag_name;
    tag->value = &value_string;
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
