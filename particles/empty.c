#include <stdlib.h>

#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"

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

static struct particle *
empty_new(struct particle *common)
{
    common->destroy = &particle_default_destroy;
    common->instantiate = &instantiate;
    return common;
}

struct particle *
empty_from_conf(const struct yml_node *node, struct particle *common)
{
    return empty_new(common);
}

bool
empty_verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

bool verify_conf(keychain_t *chain, const struct yml_node *node)
    __attribute__((weak, alias("empty_verify_conf")));
struct deco *from_conf(const struct yml_node *node, struct particle *common)
    __attribute__((weak, alias("empty_from_conf")));

#endif
