#include "particle.h"
#include <stdlib.h>

struct particle *
particle_common_new(int left_margin, int right_margin)
{
    struct particle *p = malloc(sizeof(*p));
    p->parent = NULL;
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->deco = NULL;
    return p;
}
