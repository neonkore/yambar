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

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);
    assert(tag != NULL || p->default_particle != NULL);

    if (tag == NULL)
        return p->default_particle->instantiate(p->default_particle, tags);


    const char *tag_value = tag->as_string(tag);
    for (size_t i = 0; i < p->count; i++) {
        const struct particle_map *e = &p->map[i];

        if (strcmp(e->tag_value, tag_value) != 0)
            continue;

        return e->particle->instantiate(e->particle, tags);
    }

    assert(p->default_particle != NULL);
    return p->default_particle->instantiate(p->default_particle, tags);
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
    assert(left_margin == 0 && right_margin == 0
        && "map: margins not implemented");

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
