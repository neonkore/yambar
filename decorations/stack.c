#include "underline.h"

#include <stdlib.h>

struct private {
    struct deco **decos;
    size_t count;
};

static void
destroy(struct deco *deco)
{
    struct private *d = deco->private;
    for (size_t i = 0; i < d->count; i++)
        d->decos[i]->destroy(d->decos[i]);
    free(d->decos);
    free(d);
    free(deco);
}

static void
expose(const struct deco *deco, cairo_t *cr, int x, int y, int width, int height)
{
    const struct private *d = deco->private;
    for (size_t i = 0; i < d->count; i++)
        d->decos[i]->expose(d->decos[i], cr, x, y, width, height);
}

struct deco *
deco_stack(struct deco *decos[], size_t count)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->decos = malloc(count * sizeof(priv->decos[0]));
    priv->count = count;

    for (size_t i = 0; i < count; i++)
        priv->decos[i] = decos[i];

    struct deco *deco = malloc(sizeof(*deco));
    deco->private = priv;
    deco->expose = &expose;
    deco->destroy = &destroy;

    return deco;
}
