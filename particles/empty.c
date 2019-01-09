#include "empty.h"

#include <stdlib.h>

static int
begin_expose(struct exposable *exposable)
{
    exposable->width = exposable->particle->left_margin +
        exposable->particle->right_margin;
    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    exposable_render_deco(exposable, cr, x, y, height);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    char *on_click = tags_expand_template(particle->on_click_template, tags);

    struct exposable *exposable = exposable_common_new(particle, on_click);
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;

    free(on_click);
    return exposable;
}

struct particle *
particle_empty_new(int left_margin, int right_margin,
                   const char *on_click_template)
{
    struct particle *particle = particle_common_new(
        left_margin, right_margin, on_click_template);
    particle->destroy = &particle_default_destroy;
    particle->instantiate = &instantiate;
    return particle;
}
