#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "progress_bar"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"

struct private {
    char *tag;
    int width;

    struct particle *start_marker;
    struct particle *end_marker;
    struct particle *fill;
    struct particle *empty;
    struct particle *indicator;
};

struct eprivate {
    size_t count;
    struct exposable **exposables;
};

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;

    p->start_marker->destroy(p->start_marker);
    p->end_marker->destroy(p->end_marker);
    p->fill->destroy(p->fill);
    p->empty->destroy(p->empty);
    p->indicator->destroy(p->indicator);

    free(p->tag);
    free(p);
    particle_default_destroy(particle);
}

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    for (size_t i = 0; i < e->count; i++)
        e->exposables[i]->destroy(e->exposables[i]);
    free(e->exposables);
    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;

    /* Margins */
    exposable->width = exposable->particle->left_margin +
        exposable->particle->right_margin;

    /* Sub-exposables */
    for (size_t i = 0; i < e->count; i++)
        exposable->width += e->exposables[i]->begin_expose(e->exposables[i]);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, cr, x, y, height);

    x += exposable->particle->left_margin;
    for (size_t i = 0; i < e->count; i++) {
        e->exposables[i]->expose(e->exposables[i], cr, x, y, height);
        x += e->exposables[i]->width;
    }
}

static void
on_mouse(struct exposable *exposable, struct bar *bar, enum mouse_event event,
         int x, int y)
{
    if (exposable->on_click == NULL) {
        exposable_default_on_mouse(exposable, bar, event, x, y);
        return;
    }

    /*
     * Hack-warning!
     *
     * In order to pass the *clicked* position to the on_click
     * handler, we expand the handler *again* (first time would be
     * when the particle instantiated us).
     *
     * We pass a single tag, "where", which is a percentage value.
     *
     * Keep a reference to the un-expanded string, to be able to reset
     * it after executing the handler.
     */

    char *original = exposable->on_click;

    assert(x >= 0 && x < exposable->width);
    long where = exposable->width > 0
        ? 100 * x / exposable->width
        : 0;

    struct tag_set tags = {
        .tags = (struct tag *[]){tag_new_int(NULL, "where", where)},
        .count = 1,
    };

    exposable->on_click = tags_expand_template(exposable->on_click, &tags);
    tag_set_destroy(&tags);

    /* Call default implementation, which will execute our handler */
    exposable_default_on_mouse(exposable, bar, event, x, y);

    /* Reset handler string */
    free(exposable->on_click);
    exposable->on_click = original;
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);
    assert(tag != NULL);

    long value = tag->as_int(tag);
    long min = tag->min(tag);
    long max = tag->max(tag);

    LOG_DBG("%s: value=%ld, min=%ld, max=%ld", tag->name(tag), value, min, max);

    long fill_count = max == min ? 0 : p->width * value / (max - min);
    long empty_count = p->width - fill_count;

    struct eprivate *epriv = malloc(sizeof(*epriv));
    epriv->count = (
        1 +             /* Start marker */
        fill_count +    /* Before current position */
        1 +             /* Current position indicator */
        empty_count +   /* After current position */
        1);             /* End marker */

    epriv->exposables = malloc(epriv->count * sizeof(epriv->exposables[0]));

    size_t idx = 0;
    epriv->exposables[idx++] = p->start_marker->instantiate(p->start_marker, tags);
    for (size_t i = 0; i < fill_count; i++)
        epriv->exposables[idx++] = p->fill->instantiate(p->fill, tags);
    epriv->exposables[idx++] = p->indicator->instantiate(p->indicator, tags);
    for (size_t i = 0; i < empty_count; i++)
        epriv->exposables[idx++] = p->empty->instantiate(p->empty, tags);
    epriv->exposables[idx++] = p->end_marker->instantiate(p->end_marker, tags);

    assert(idx == epriv->count);

    char *on_click = tags_expand_template(particle->on_click_template, tags);

    struct exposable *exposable = exposable_common_new(particle, on_click);
    free(on_click);

    exposable->private = epriv;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    exposable->on_mouse = &on_mouse;

    enum tag_realtime_unit rt = tag->realtime(tag);

    if (rt == TAG_REALTIME_NONE)
        return exposable;

#if 0
    long units_per_segment = (max - min) / p->width;
    long units_filled = fill_count * (max - min) / p->width;
    long units_til_next_segment = units_per_segment - (value - units_filled);

    LOG_DBG("tag: %s, value: %ld, "
            "units-per-segment: %ld, units-filled: %ld, units-til-next: %ld",
            tag->name(tag), value,
            units_per_segment, units_filled, units_til_next_segment);
#else
    double units_per_segment = (double)(max - min) / p->width;
    double units_filled = fill_count * units_per_segment;
    double units_til_next_segment = units_per_segment - ((double)value - units_filled);

    LOG_DBG("tag: %s, value: %ld, "
            "units-per-segment: %f, units-filled: %f, units-til-next: %f",
            tag->name(tag), value,
            units_per_segment, units_filled, units_til_next_segment);

#endif

    if (!tag->refresh_in(tag, units_til_next_segment))
        LOG_WARN("failed to schedule update of tag");

    return exposable;
}

static struct particle *
progress_bar_new(struct particle *common, const char *tag, int width,
                 struct particle *start_marker, struct particle *end_marker,
                 struct particle *fill, struct particle *empty,
                 struct particle *indicator)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->tag = strdup(tag);
    priv->width = width;
    priv->start_marker = start_marker;
    priv->end_marker = end_marker;
    priv->fill = fill;
    priv->empty = empty;
    priv->indicator = indicator;

    common->private = priv;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

struct particle *
progress_bar_from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *tag = yml_get_value(node, "tag");
    const struct yml_node *length = yml_get_value(node, "length");
    const struct yml_node *start = yml_get_value(node, "start");
    const struct yml_node *end = yml_get_value(node, "end");
    const struct yml_node *fill = yml_get_value(node, "fill");
    const struct yml_node *empty = yml_get_value(node, "empty");
    const struct yml_node *indicator = yml_get_value(node, "indicator");

    struct conf_inherit inherited = {
        .font = common->font,
        .foreground = common->foreground,
    };

    return progress_bar_new(
        common,
        yml_value_as_string(tag),
        yml_value_as_int(length),
        conf_to_particle(start, inherited),
        conf_to_particle(end, inherited),
        conf_to_particle(fill, inherited),
        conf_to_particle(empty, inherited),
        conf_to_particle(indicator, inherited));
}

bool
progress_bar_verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"tag", true, &conf_verify_string},
        {"length", true, &conf_verify_int},
        /* TODO: make these optional? Default to empty */
        {"start", true, &conf_verify_particle},
        {"end", true, &conf_verify_particle},
        {"fill", true, &conf_verify_particle},
        {"empty", true, &conf_verify_particle},
        {"indicator", true, &conf_verify_particle},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

bool verify_conf(keychain_t *chain, const struct yml_node *node)
    __attribute__((weak, alias("progress_bar_verify_conf")));
struct deco *from_conf(const struct yml_node *node, struct particle *common)
    __attribute__((weak, alias("progress_bar_from_conf")));

#endif
