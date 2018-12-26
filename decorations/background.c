#include "background.h"

#include <stdlib.h>

struct private {
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
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);
}

struct deco *
deco_background(struct rgba color)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->color = color;

    struct deco *deco = malloc(sizeof(*deco));
    deco->private = priv;
    deco->expose = &expose;
    deco->destroy = &destroy;

    return deco;
}
