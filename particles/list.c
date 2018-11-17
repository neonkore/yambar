#include "list.h"
#include <stdlib.h>

struct particle_private {
    struct particle **particles;
    size_t count;
    int left_spacing, right_spacing;
};

struct exposable_private {
    struct exposable **exposables;
    int *widths;
    size_t count;
    int left_spacing, right_spacing;
};

static int
begin_expose(const struct exposable *exposable, cairo_t *cr)
{
    const struct exposable_private *e = exposable->private;

    int width = exposable->particle->left_margin;

    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        e->widths[i] = ee->begin_expose(ee, cr);

        width += e->left_spacing + e->widths[i] + e->right_spacing;
    }

    width -= e->left_spacing + e->right_spacing;
    width += exposable->particle->right_margin;
    return width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct exposable_private *e = exposable->private;

    int left_margin = exposable->particle->left_margin;
    int left_spacing = e->left_spacing;
    int right_spacing = e->right_spacing;

    x += left_margin - left_spacing;
    for (size_t i = 0; i < e->count; i++) {
        const struct exposable *ee = e->exposables[i];
        ee->expose(ee, cr, x + left_spacing, y, height);
        x += left_spacing + e->widths[i] + right_spacing;
    }
}

static void
exposable_destroy(struct exposable *exposable)
{
    struct exposable_private *e = exposable->private;
    for (size_t i = 0; i < e->count; i++)
        e->exposables[i]->destroy(e->exposables[i]);

    free(e->exposables);
    free(e->widths);
    free(e);
    free(exposable);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct particle_private *p = particle->private;

    struct exposable_private *e = malloc(sizeof(*e));
    e->exposables = malloc(p->count * sizeof(*e->exposables));
    e->widths = malloc(p->count * sizeof(*e->widths));
    e->count = p->count;
    e->left_spacing = p->left_spacing;
    e->right_spacing = p->right_spacing;

    for (size_t i = 0; i < p->count; i++) {
        const struct particle *pp = p->particles[i];
        e->exposables[i] = pp->instantiate(pp, tags);
    }

    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->private = e;
    exposable->particle = particle;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    return exposable;
}

static void
particle_destroy(struct particle *particle)
{
    struct particle_private *p = particle->private;
    for (size_t i = 0; i < p->count; i++)
        p->particles[i]->destroy(p->particles[i]);
    free(p->particles);
    free(p);
    free(particle);
}

struct particle *
particle_list_new(
    struct particle *particles[], size_t count,
    int left_spacing, int right_spacing, int left_margin, int right_margin)
{
    struct particle_private *p = malloc(sizeof(*p));
    p->particles = malloc(count * sizeof(p->particles[0]));
    p->count = count;
    p->left_spacing = left_spacing;
    p->right_spacing = right_spacing;

    for (size_t i = 0; i < count; i++)
        p->particles[i] = particles[i];

    struct particle *particle = particle_common_new(left_margin, right_margin);

    particle->private = p;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    /* Claim ownership */
    for (size_t i = 0; i < count; i++)
        p->particles[i]->parent = particle;

    return particle;
}
