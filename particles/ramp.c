#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"

struct private {
    char *tag;
    struct particle **particles;
    size_t count;
};

struct eprivate {
    struct exposable *exposable;
};

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    e->exposable->destroy(e->exposable);

    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    struct eprivate *e = exposable->private;

    exposable->width = (
        exposable->particle->left_margin +
        e->exposable->begin_expose(e->exposable, cr) +
        exposable->particle->right_margin);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, cr, x, y, height);
    e->exposable->expose(
        e->exposable, cr, x + exposable->particle->left_margin, y, height);
}

static void
on_mouse(struct exposable *exposable, struct bar *bar, enum mouse_event event,
         int x, int y)
{
    const struct particle *p = exposable->particle;
    const struct eprivate *e = exposable->private;

    if (exposable->on_click != NULL) {
        /* We have our own handler */
        exposable_default_on_mouse(exposable, bar, event, x, y);
        return;
    }

    int px = p->left_margin;
    if (x >= px && x < px + e->exposable->width) {
        if (e->exposable->on_mouse != NULL)
            e->exposable->on_mouse(e->exposable, bar, event, x - px, y);
        return;
    }

    /* In the left- or right margin */
    exposable_default_on_mouse(exposable, bar, event, x, y);
}

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;

    for (size_t i = 0; i < p->count; i++)
        p->particles[i]->destroy(p->particles[i]);

    free(p->tag);
    free(p->particles);
    free(p);
    particle_default_destroy(particle);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);
    assert(tag != NULL);

    assert(p->count > 0);

    long value = tag->as_int(tag);
    long min = tag->min(tag);
    long max = tag->max(tag);

    assert(value >= min && value <= max);
    assert(max >= min);

    size_t idx = 0;
    if (max - min > 0)
        idx = p->count * (value - min) / (max - min);

    if (idx == p->count)
        idx--;
    /*
     * printf("ramp: value: %lu, min: %lu, max: %lu, progress: %f, idx: %zu\n",
     *        value, min, max, progress, idx);
     */
    assert(idx >= 0 && idx < p->count);

    struct particle *pp = p->particles[idx];

    struct eprivate *e = calloc(1, sizeof(*e));
    e->exposable = pp->instantiate(pp, tags);

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

static struct particle *
ramp_new(struct particle *common, const char *tag,
         struct particle *particles[], size_t count)
{

    struct private *priv = calloc(1, sizeof(*priv));
    priv->tag = strdup(tag);
    priv->particles = malloc(count * sizeof(priv->particles[0]));
    priv->count = count;

    for (size_t i = 0; i < count; i++)
        priv->particles[i] = particles[i];

    common->private = priv;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *items = yml_get_value(node, "items");

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

    return ramp_new(common, yml_value_as_string(tag), parts, count);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"tag", true, &conf_verify_string},
        {"items", true, &conf_verify_particle_list_items},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct particle_iface particle_ramp_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_ramp_iface")));
#endif
