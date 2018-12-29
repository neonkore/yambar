#include "particle.h"
#include <stdlib.h>

void
particle_default_destroy(struct particle *particle)
{
    if (particle->deco != NULL)
        particle->deco->destroy(particle->deco);
    free(particle);
}

struct particle *
particle_common_new(int left_margin, int right_margin)
{
    struct particle *p = malloc(sizeof(*p));
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->deco = NULL;
    return p;
}
