#include "dynlist-exposable.h"

#include <stdlib.h>

struct private {
    int left_spacing;
    int right_spacing;

    struct exposable **exposables;
    size_t count;
    int *widths;
};

static int
dynlist_begin_expose(const struct exposable *exposable, cairo_t *cr)
{
    const struct private *e = exposable->private;

   int width = 0;

    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        e->widths[i] = ee->begin_expose(ee, cr);

        width += e->left_spacing + e->widths[i] + e->right_spacing;
    }

    width -= e->left_spacing + e->right_spacing;
    return width;
}

static void
dynlist_expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct private *e = exposable->private;

    int left_spacing = e->left_spacing;
    int right_spacing = e->right_spacing;

    x -= left_spacing;

    for (size_t i = 0; i < e->count; i++) {
        const struct exposable *ee = e->exposables[i];
        ee->expose(ee, cr, x + left_spacing, y, height);
        x += left_spacing + e->widths[i] + right_spacing;
    }
}

static void
dynlist_destroy(struct exposable *exposable)
{
    struct private *e = exposable->private;
    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        ee->destroy(ee);
    }

    free(e->exposables);
    free(e->widths);
    free(e);
    free(exposable);
}

struct exposable *
dynlist_exposable_new(struct exposable **exposables, size_t count,
                      int left_spacing, int right_spacing)
{
    struct private *e = malloc(sizeof(*e));
    e->count = count;
    e->exposables = malloc(count * sizeof(e->exposables[0]));
    e->widths = malloc(count * sizeof(e->widths[0]));
    e->left_spacing = left_spacing;
    e->right_spacing = right_spacing;

    for (size_t i = 0; i < count; i++)
        e->exposables[i] = exposables[i];

    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->private = e;
    exposable->particle = NULL;
    exposable->destroy = &dynlist_destroy;
    exposable->begin_expose = &dynlist_begin_expose;
    exposable->expose = &dynlist_expose;
    return exposable;
}
