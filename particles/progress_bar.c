#include "progress_bar.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct private {
    char *tag;
    int width;

    struct particle *start_marker;
    struct particle *end_marker;
    struct particle *fill;
    struct particle *empty;
    struct particle *indicator;
};

struct exposable_private {
    size_t count;
    struct exposable **exposables;
};

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;

    p->start_marker->destroy(p->start_marker);
    p->end_marker->destroy(p->end_marker);
    p->fill->destroy(p->fill);
    p->empty->destroy(p->empty);
    p->indicator->destroy(p->indicator);

    free(p->tag);
    free(p);
    particle_default_destroy(particle);
}

static void
exposable_destroy(struct exposable *exposable)
{
    struct exposable_private *e = exposable->private;
    for (size_t i = 0; i < e->count; i++)
        e->exposables[i]->destroy(e->exposables[i]);
    free(e->exposables);
    free(e);
    free(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    struct exposable_private *e = exposable->private;

    /* Margins */
    exposable->width = exposable->particle->left_margin +
        exposable->particle->right_margin;

    /* Sub-exposables */
    for (size_t i = 0; i < e->count; i++)
        exposable->width += e->exposables[i]->begin_expose(e->exposables[i], cr);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct exposable_private *e = exposable->private;

    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, cr, x, y, exposable->width, height);

    x += exposable->particle->left_margin;
    for (size_t i = 0; i < e->count; i++) {
        e->exposables[i]->expose(e->exposables[i], cr, x, y, height);
        x += e->exposables[i]->width;
    }
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);
    assert(tag != NULL);

    long value = tag->as_int(tag);
    long min = tag->min(tag);
    long max = tag->max(tag);

    long fill_count = max == min ? 0 : p->width * value / (max - min);
    long empty_count = p->width - fill_count;

    struct exposable_private *epriv = malloc(sizeof(*epriv));
    epriv->count = (
        1 +             /* Start marker */
        fill_count +    /* Before current position */
        1 +             /* Current position indicator */
        empty_count +   /* After current position */
        1);             /* End marker */

    epriv->exposables = malloc(epriv->count * sizeof(epriv->exposables[0]));

    size_t idx = 0;
    epriv->exposables[idx++] = p->start_marker->instantiate(p->start_marker, tags);
    for (size_t i = 0; i < fill_count; i++)
        epriv->exposables[idx++] = p->fill->instantiate(p->fill, tags);
    epriv->exposables[idx++] = p->indicator->instantiate(p->indicator, tags);
    for (size_t i = 0; i < empty_count; i++)
        epriv->exposables[idx++] = p->empty->instantiate(p->empty, tags);
    epriv->exposables[idx++] = p->end_marker->instantiate(p->end_marker, tags);

    assert(idx == epriv->count);

    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->particle = particle;
    exposable->private = epriv;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;

    return exposable;
}

struct particle *
particle_progress_bar_new(const char *tag, int width,
                          struct particle *start_marker,
                          struct particle *end_marker,
                          struct particle *fill, struct particle *empty,
                          struct particle *indicator,
                          int left_margin, int right_margin)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->tag = strdup(tag);
    priv->width = width;
    priv->start_marker = start_marker;
    priv->end_marker = end_marker;
    priv->fill = fill;
    priv->empty = empty;
    priv->indicator = indicator;

    struct particle *particle = particle_common_new(left_margin, right_margin);
    particle->private = priv;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    return particle;
}
