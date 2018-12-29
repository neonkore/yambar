#include "particle.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "particle"
#define LOG_ENABLE_DBG 1
#include "log.h"
#include "bar.h"

void
particle_default_destroy(struct particle *particle)
{
    if (particle->deco != NULL)
        particle->deco->destroy(particle->deco);
    free(particle->on_click_template);
    free(particle);
}

struct particle *
particle_common_new(int left_margin, int right_margin,
                    const char *on_click_template)
{
    struct particle *p = malloc(sizeof(*p));
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->on_click_template = on_click_template != NULL ? strdup(on_click_template) : NULL;
    p->deco = NULL;
    return p;
}

struct exposable *
exposable_common_new(const struct particle *particle, const char *on_click)
{
    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->particle = particle;
    exposable->private = NULL;
    exposable->width = 0;
    exposable->on_click = on_click != NULL ? strdup(on_click) : NULL;
    exposable->destroy = &exposable_default_destroy;
    exposable->on_mouse = &exposable_default_on_mouse;
    exposable->begin_expose = NULL;
    exposable->expose = NULL;
    return exposable;
}

void
exposable_default_destroy(struct exposable *exposable)
{
    free(exposable->on_click);
    free(exposable);
}

void
exposable_default_on_mouse(struct exposable *exposable, struct bar *bar,
                           enum mouse_event event, int x, int y)
{
    LOG_DBG("on_mouse: exposable=%p, event=%s, x=%d, y=%d", exposable,
            event == ON_MOUSE_MOTION ? "motion" : "click", x, y);

    assert(exposable->particle != NULL);

    if (exposable->on_click == NULL)
        bar->set_cursor(bar, "left_ptr");
    else
        bar->set_cursor(bar, "hand2");
}
