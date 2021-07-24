#include "dynlist.h"

#include <stdlib.h>

#define LOG_MODULE "dynlist"
#include "../log.h"
#include "../particle.h"

struct private {
    int left_spacing;
    int right_spacing;

    struct exposable **exposables;
    size_t count;
    int *widths;
};

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

static int
dynlist_begin_expose(struct exposable *exposable)
{
    const struct private *e = exposable->private;

    exposable->width = 0;

    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        e->widths[i] = ee->begin_expose(ee);

        exposable->width += e->left_spacing + e->widths[i] + e->right_spacing;
    }

    exposable->width -= e->left_spacing + e->right_spacing;
    return exposable->width;
}

static void
dynlist_expose(const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height)
{
    const struct private *e = exposable->private;

    int left_spacing = e->left_spacing;
    int right_spacing = e->right_spacing;

    x -= left_spacing;

    for (size_t i = 0; i < e->count; i++) {
        const struct exposable *ee = e->exposables[i];
        ee->expose(ee, pix, x + left_spacing, y, height);
        x += left_spacing + e->widths[i] + right_spacing;
    }
}

static void
on_mouse(struct exposable *exposable, struct bar *bar,
         enum mouse_event event, enum mouse_button btn, int x, int y)
{
    const struct private *e = exposable->private;

    if (exposable->on_click[btn] != NULL) {
        exposable_default_on_mouse(exposable, bar, event, btn, x, y);
        return;
    }

    int px = /*p->left_margin;*/0;
    for (size_t i = 0; i < e->count; i++) {
        if (x >= px && x < px + e->exposables[i]->width) {
            if (e->exposables[i]->on_mouse != NULL) {
                e->exposables[i]->on_mouse(
                    e->exposables[i], bar, event, btn, x - px, y);
            }
            return;
        }

        px += e->left_spacing + e->exposables[i]->width + e->right_spacing;
    }

    LOG_DBG("on_mouse missed all sub-particles");
    exposable_default_on_mouse(exposable, bar, event, btn, x, y);
}

struct exposable *
dynlist_exposable_new(struct exposable **exposables, size_t count,
                      int left_spacing, int right_spacing)
{
    struct private *e = calloc(1, sizeof(*e));
    e->count = count;
    e->exposables = malloc(count * sizeof(e->exposables[0]));
    e->widths = calloc(count, sizeof(e->widths[0]));
    e->left_spacing = left_spacing;
    e->right_spacing = right_spacing;

    for (size_t i = 0; i < count; i++)
        e->exposables[i] = exposables[i];

    struct exposable *exposable = exposable_common_new(NULL, NULL);
    exposable->private = e;
    exposable->destroy = &dynlist_destroy;
    exposable->begin_expose = &dynlist_begin_expose;
    exposable->expose = &dynlist_expose;
    exposable->on_mouse = &on_mouse;
    return exposable;
}
