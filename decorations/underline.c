#include <stdlib.h>

#include "../config.h"
#include "../config-verify.h"
#include "../decoration.h"

struct private {
    int size;
    struct rgba color;
};

static void
destroy(struct deco *deco)
{
    struct private *d = deco->private;
    free(d);
    free(deco);
}

static void
expose(const struct deco *deco, cairo_t *cr, int x, int y, int width, int height)
{
    const struct private *d = deco->private;
    cairo_set_source_rgba(
        cr, d->color.red, d->color.green, d->color.blue, d->color.alpha);
    cairo_rectangle(cr, x, y + height - d->size, width, d->size);
    cairo_fill(cr);
}

static struct deco *
underline_new(int size, struct rgba color)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->size = size;
    priv->color = color;

    struct deco *deco = malloc(sizeof(*deco));
    deco->private = priv;
    deco->expose = &expose;
    deco->destroy = &destroy;

    return deco;
}

struct deco *
underline_from_conf(const struct yml_node *node)
{
    const struct yml_node *size = yml_get_value(node, "size");
    const struct yml_node *color = yml_get_value(node, "color");
    return underline_new(yml_value_as_int(size), conf_to_color(color));
}

bool
underline_verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"size", true, &conf_verify_int},
        {"color", true, &conf_verify_color},
        DECORATION_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

bool verify_conf(keychain_t *chain, const struct yml_node *node)
    __attribute__((weak, alias("underline_verify_conf")));
struct deco *from_conf(const struct yml_node *node)
    __attribute__((weak, alias("underline_from_conf")));

#endif
