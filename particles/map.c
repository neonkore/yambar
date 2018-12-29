#include "map.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct private {
    char *tag;
    struct particle *default_particle;
    struct particle_map *map;
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
    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, cr, x, y, exposable->width, height);

    struct eprivate *e = exposable->private;
    e->exposable->expose(
        e->exposable, cr, x + exposable->particle->left_margin, y, height);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);
    assert(tag != NULL || p->default_particle != NULL);

    if (tag == NULL)
        return p->default_particle->instantiate(p->default_particle, tags);


    const char *tag_value = tag->as_string(tag);
    struct particle *pp = NULL;

    for (size_t i = 0; i < p->count; i++) {
        const struct particle_map *e = &p->map[i];

        if (strcmp(e->tag_value, tag_value) != 0)
            continue;

        pp = e->particle;
        break;
    }

    if (pp == NULL) {
        assert(p->default_particle != NULL);
        pp = p->default_particle;
    }

    struct eprivate *e = malloc(sizeof(*e));
    e->exposable = pp->instantiate(pp, tags);

    char *on_click = tags_expand_template(particle->on_click_template, tags);
    struct exposable *exposable = exposable_common_new(particle, on_click);
    exposable->private = e;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;

    free(on_click);
    return exposable;
}

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;

    if (p->default_particle != NULL)
        p->default_particle->destroy(p->default_particle);

    for (size_t i = 0; i < p->count; i++) {
        struct particle *pp = p->map[i].particle;
        pp->destroy(pp);
        free((char *)p->map[i].tag_value);
    }

    free(p->map);
    free(p->tag);
    free(p);
    particle_default_destroy(particle);
}

struct particle *
particle_map_new(const char *tag, const struct particle_map *particle_map,
                 size_t count, struct particle *default_particle,
                 int left_margin, int right_margin)
{
    struct particle *particle = particle_common_new(
        left_margin, right_margin, NULL);
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    struct private *priv = malloc(sizeof(*priv));
    priv->tag = strdup(tag);
    priv->default_particle = default_particle;
    priv->count = count;
    priv->map = malloc(count * sizeof(priv->map[0]));

    for (size_t i = 0; i < count; i++) {
        priv->map[i].tag_value = strdup(particle_map[i].tag_value);
        priv->map[i].particle = particle_map[i].particle;
    }

    particle->private = priv;
    return particle;
}
