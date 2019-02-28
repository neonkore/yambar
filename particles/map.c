#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "map"
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"

struct particle_map {
    const char *tag_value;
    struct particle *particle;
};

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

static struct particle *
map_new(struct particle *common, const char *tag,
        const struct particle_map particle_map[], size_t count,
        struct particle *default_particle)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->tag = strdup(tag);
    priv->default_particle = default_particle;
    priv->count = count;
    priv->map = malloc(count * sizeof(priv->map[0]));

    for (size_t i = 0; i < count; i++) {
        priv->map[i].tag_value = strdup(particle_map[i].tag_value);
        priv->map[i].particle = particle_map[i].particle;
    }

    common->private = priv;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

static bool
verify_map_values(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            conf_err_prefix(chain, node));
        return false;
    }

    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", conf_err_prefix(chain, it.key));
            return false;
        }

        if (!conf_verify_particle(chain_push(chain, key), it.value))
            return false;

        chain_pop(chain);
    }

    return true;
}

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *values = yml_get_value(node, "values");
    const struct yml_node *def = yml_get_value(node, "default");

    struct particle_map particle_map[yml_dict_length(values)];

    struct conf_inherit inherited = {
        .font = common->font,
        .foreground = common->foreground
    };

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(values);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        particle_map[idx].tag_value = yml_value_as_string(it.key);
        particle_map[idx].particle = conf_to_particle(it.value, inherited);
    }

    struct particle *default_particle = def != NULL
        ? conf_to_particle(def, inherited) : NULL;

    return map_new(
        common, yml_value_as_string(tag), particle_map, yml_dict_length(values),
        default_particle);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"tag", true, &conf_verify_string},
        {"values", true, &verify_map_values},
        {"default", false, &conf_verify_particle},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct particle_iface particle_map_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_map_iface")));
#endif
