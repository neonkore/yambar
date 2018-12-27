#include "empty.h"

#include <stdlib.h>

static void
exposable_destroy(struct exposable *exposable)
{
    free(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    exposable->width = exposable->particle->left_margin +
        exposable->particle->right_margin;
    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, cr, x, y, exposable->width, height);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->particle = particle;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    return exposable;
}

struct particle *
particle_empty_new(int left_margin, int right_margin)
{
    struct particle *particle = particle_common_new(left_margin, right_margin);
    particle->destroy = &particle_default_destroy;
    particle->instantiate = &instantiate;
    return particle;
}
