#include "map.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct map {
    char *tag;
    struct particle *default_particle;
    struct particle_map *map;
    size_t count;
};

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct map *map = particle->private;
    const struct tag *tag = tag_for_name(tags, map->tag);
    assert(tag != NULL || map->default_particle != NULL);

    if (tag == NULL)
        return map->default_particle->instantiate(map->default_particle, tags);


    const char *tag_value = tag->as_string(tag);
    for (size_t i = 0; i < map->count; i++) {
        const struct particle_map *e = &map->map[i];

        if (strcmp(e->tag_value, tag_value) != 0)
            continue;

        return e->particle->instantiate(e->particle, tags);
    }

    assert(map->default_particle != NULL);
    return map->default_particle->instantiate(map->default_particle, tags);
}

static void
particle_destroy(struct particle *particle)
{
    struct map *map = particle->private;

    if (map->default_particle != NULL)
        map->default_particle->destroy(map->default_particle);

    for (size_t i = 0; i < map->count; i++) {
        struct particle *p = map->map[i].particle;
        p->destroy(p);
        free((char *)map->map[i].tag_value);
    }

    free(map->map);
    free(map->tag);
    free(map);
    particle_default_destroy(particle);
}

struct particle *
particle_map_new(const char *tag, const struct particle_map *particle_map,
                 size_t count, struct particle *default_particle)
{
    struct particle *particle = particle_common_new(0, 0);
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    struct map *map = malloc(sizeof(*map));
    map->tag = strdup(tag);
    map->default_particle = default_particle;
    map->count = count;
    map->map = malloc(count * sizeof(map->map[0]));

    for (size_t i = 0; i < count; i++) {
        map->map[i].tag_value = strdup(particle_map[i].tag_value);
        map->map[i].particle = particle_map[i].particle;
    }

    particle->private = map;
    return particle;
}
