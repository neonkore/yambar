#include <stdlib.h>

#define LOG_MODULE "list"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"

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
begin_expose(struct exposable *exposable)
{
    const struct eprivate *e = exposable->private;
    bool have_at_least_one = false;

    exposable->width = 0;

    for (size_t i = 0; i < e->count; i++) {
        struct exposable *ee = e->exposables[i];
        e->widths[i] = ee->begin_expose(ee);

        assert(e->widths[i] >= 0);

        if (e->widths[i] > 0) {
            exposable->width += e->left_spacing + e->widths[i] + e->right_spacing;
            have_at_least_one = true;
        }
    }

    if (have_at_least_one) {
        exposable->width -= e->left_spacing + e->right_spacing;
        exposable->width += exposable->particle->left_margin;
        exposable->width += exposable->particle->right_margin;
    } else
        assert(exposable->width == 0);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height)
{
    const struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, pix, x, y, height);

    int left_margin = exposable->particle->left_margin;
    int left_spacing = e->left_spacing;
    int right_spacing = e->right_spacing;

    x += left_margin - left_spacing;
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
    const struct particle *p = exposable->particle;
    const struct eprivate *e = exposable->private;

    if ((event == ON_MOUSE_MOTION &&
         exposable->particle->have_on_click_template) ||
        exposable->on_click[btn] != NULL)
    {
        /* We have our own handler */
        exposable_default_on_mouse(exposable, bar, event, btn, x, y);
        return;
    }

    int px = p->left_margin;
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

    /* We're between sub-particles (or in the left/right margin) */
    exposable_default_on_mouse(exposable, bar, event, btn, x, y);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;

    struct eprivate *e = calloc(1, sizeof(*e));
    e->exposables = malloc(p->count * sizeof(*e->exposables));
    e->widths = calloc(p->count, sizeof(*e->widths));
    e->count = p->count;
    e->left_spacing = p->left_spacing;
    e->right_spacing = p->right_spacing;

    for (size_t i = 0; i < p->count; i++) {
        const struct particle *pp = p->particles[i];
        e->exposables[i] = pp->instantiate(pp, tags);
        assert(e->exposables[i] != NULL);
    }

    struct exposable *exposable = exposable_common_new(particle, tags);
    exposable->private = e;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    exposable->on_mouse = &on_mouse;
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
particle_list_new(struct particle *common,
                  struct particle *particles[], size_t count,
                  int left_spacing, int right_spacing)
{
    struct private *p = calloc(1, sizeof(*p));
    p->particles = malloc(count * sizeof(p->particles[0]));
    p->count = count;
    p->left_spacing = left_spacing;
    p->right_spacing = right_spacing;

    for (size_t i = 0; i < count; i++)
        p->particles[i] = particles[i];

    common->private = p;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *items = yml_get_value(node, "items");
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *_left_spacing = yml_get_value(node, "left-spacing");
    const struct yml_node *_right_spacing = yml_get_value(node, "right-spacing");

    int left_spacing = spacing != NULL ? yml_value_as_int(spacing) :
        _left_spacing != NULL ? yml_value_as_int(_left_spacing) : 0;
    int right_spacing = spacing != NULL ? yml_value_as_int(spacing) :
        _right_spacing != NULL ? yml_value_as_int(_right_spacing) : 2;

    size_t count = yml_list_length(items);
    struct particle *parts[count];

    size_t idx = 0;
    for (struct yml_list_iter it = yml_list_iter(items);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        parts[idx] = conf_to_particle(
            it.node, (struct conf_inherit){common->font, common->foreground});
    }

    return particle_list_new(common, parts, count, left_spacing, right_spacing);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"items", true, &conf_verify_particle_list_items},
        {"spacing", false, &conf_verify_unsigned},
        {"left-spacing", false, &conf_verify_unsigned},
        {"right-spacing", false, &conf_verify_unsigned},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct particle_iface particle_list_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_list_iface")));
#endif
