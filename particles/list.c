#include "list.h"
#include <stdlib.h>

#define LOG_MODULE "list"
#define LOG_ENABLE_DBG 1
#include "../log.h"

struct private {
    struct particle **particles;
    size_t count;
    int left_spacing, right_spacing;
};

struct eprivate {
    struct exposable **exposables;
    int *widths;
    size_t count;
    int left_spacing, right_spacing;
};


static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    for (size_t i = 0; i < e->count; i++)
        e->exposables[i]->destroy(e->exposables[i]);

    free(e->exposables);
    free(e->widths);
    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    const struct eprivate *e = exposable->private;

    exposable->width = exposable->particle->left_margin;

    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        e->widths[i] = ee->begin_expose(ee, cr);

        exposable->width += e->left_spacing + e->widths[i] + e->right_spacing;
    }

    exposable->width -= e->left_spacing + e->right_spacing;
    exposable->width += exposable->particle->right_margin;
    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, cr, x, y, height);

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
on_mouse(struct exposable *exposable, struct bar *bar,
         enum mouse_event event, int x, int y)
{
    const struct particle *p = exposable->particle;
    const struct eprivate *e = exposable->private;

    if (exposable->on_click != NULL) {
        /* We have our own handler */
        exposable_default_on_mouse(exposable, bar, event, x, y);
        return;
    }

    int px = p->left_margin;
    for (size_t i = 0; i < e->count; i++) {
        if (x >= px && x < px + e->exposables[i]->width) {
            if (e->exposables[i]->on_mouse != NULL) {
                e->exposables[i]->on_mouse(
                    e->exposables[i], bar, event, x - px, y);
            }
            return;
        }

        px += e->left_spacing + e->exposables[i]->width + e->right_spacing;
    }

    /* We're between sub-particles (or in the left/right margin) */
    exposable_default_on_mouse(exposable, bar, event, x, y);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;

    struct eprivate *e = malloc(sizeof(*e));
    e->exposables = malloc(p->count * sizeof(*e->exposables));
    e->widths = malloc(p->count * sizeof(*e->widths));
    e->count = p->count;
    e->left_spacing = p->left_spacing;
    e->right_spacing = p->right_spacing;

    for (size_t i = 0; i < p->count; i++) {
        const struct particle *pp = p->particles[i];
        e->exposables[i] = pp->instantiate(pp, tags);
    }

    char *on_click = tags_expand_template(particle->on_click_template, tags);

    struct exposable *exposable = exposable_common_new(particle, on_click);
    exposable->private = e;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    exposable->on_mouse = &on_mouse;

    free(on_click);
    return exposable;
}

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;
    for (size_t i = 0; i < p->count; i++)
        p->particles[i]->destroy(p->particles[i]);
    free(p->particles);
    free(p);
    particle_default_destroy(particle);
}

struct particle *
particle_list_new(
    struct particle *particles[], size_t count,
    int left_spacing, int right_spacing, int left_margin, int right_margin,
    const char *on_click_template)
{
    struct private *p = malloc(sizeof(*p));
    p->particles = malloc(count * sizeof(p->particles[0]));
    p->count = count;
    p->left_spacing = left_spacing;
    p->right_spacing = right_spacing;

    for (size_t i = 0; i < count; i++)
        p->particles[i] = particles[i];

    struct particle *particle = particle_common_new(
        left_margin, right_margin, on_click_template);

    particle->private = p;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    return particle;
}
