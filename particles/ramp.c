#include "ramp.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

struct ramp {
    char *tag;
    struct particle **particles;
    size_t count;
};

static void
particle_destroy(struct particle *particle)
{
    struct ramp *ramp = particle->private;

    for (size_t i = 0; i < ramp->count; i++)
        ramp->particles[i]->destroy(ramp->particles[i]);

    free(ramp->tag);
    free(ramp->particles);
    free(ramp);
    particle_default_destroy(particle);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct ramp *ramp = particle->private;
    const struct tag *tag = tag_for_name(tags, ramp->tag);
    assert(tag != NULL);

    assert(ramp->count > 0);

    long value = tag->as_int(tag);
    long min = tag->min(tag);
    long max = tag->max(tag);

    assert(value >= min && value <= max);
    assert(max >= min);

    size_t idx = 0;
    if (max - min > 0)
        idx = ramp->count * value / (max - min);

    if (idx == ramp->count)
        idx--;
    /*
     * printf("ramp: value: %lu, min: %lu, max: %lu, progress: %f, idx: %zu\n",
     *        value, min, max, progress, idx);
     */
    assert(idx >= 0 && idx < ramp->count);

    struct particle *p = ramp->particles[idx];
    return p->instantiate(p, tags);
}

struct particle *
particle_ramp_new(const char *tag, struct particle *particles[], size_t count)
{
    struct particle *particle = particle_common_new(0, 0);
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    struct ramp *ramp = malloc(sizeof(*ramp));
    ramp->tag = strdup(tag);
    ramp->particles = calloc(count, sizeof(ramp->particles[0]));
    ramp->count = count;

    for (size_t i = 0; i < count; i++)
        ramp->particles[i] = particles[i];

    particle->private = ramp;
    return particle;
}
