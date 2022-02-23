#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "progress_bar"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"

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
    bool have_at_least_one = false;

    exposable->width = 0;

    /* Sub-exposables */
    for (size_t i = 0; i < e->count; i++) {
        int width = e->exposables[i]->begin_expose(e->exposables[i]);

        assert(width >= 0);
        if (width >= 0) {
            exposable->width += width;
            have_at_least_one = true;
        }
    }

    /* Margins */
    if (have_at_least_one) {
        exposable->width += exposable->particle->left_margin +
            exposable->particle->right_margin;
    } else
        assert(exposable->width == 0);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height)
{
    const struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, pix, x, y, height);

    x += exposable->particle->left_margin;
    for (size_t i = 0; i < e->count; i++) {
        e->exposables[i]->expose(e->exposables[i], pix, x, y, height);
        x += e->exposables[i]->width;
    }
}

static void
on_mouse(struct exposable *exposable, struct bar *bar, enum mouse_event event,
         enum mouse_button btn, int x, int y)
{
    const struct particle *p = exposable->particle;
    const struct eprivate *e = exposable->private;

    /* Start of empty/fill cells */
    int x_offset = p->left_margin + e->exposables[0]->width;

    /* Mouse is *before* progress-bar? */
    if (x < x_offset) {
        if (x >= p->left_margin) {
            /* Mouse is over the start-marker */
            struct exposable *start = e->exposables[0];
            if (start->on_mouse != NULL)
                start->on_mouse(start, bar, event, btn, x - p->left_margin, y);
        } else {
            /* Mouse if over left margin */
            bar->set_cursor(bar, "left_ptr");
        }
        return;
    }

    /* Size of the clickable area (the empty/fill cells) */
    int clickable_width = 0;
    for (size_t i = 1; i < e->count - 1; i++)
        clickable_width += e->exposables[i]->width;

    /* Mouse is *after* progress-bar? */
    if (x - x_offset > clickable_width) {
        if (x - x_offset - clickable_width < e->exposables[e->count - 1]->width) {
            /* Mouse is over the end-marker */
            struct exposable *end = e->exposables[e->count - 1];
            if (end->on_mouse != NULL)
                end->on_mouse(end, bar, event, btn, x - x_offset - clickable_width, y);
        } else {
            /* Mouse is over the right margin */
            bar->set_cursor(bar, "left_ptr");
        }
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
     * Keep a reference to the un-expanded string, to be able to
     * reset it after executing the handler.
     *
     * Note that we only consider the actual progress bar to be
     * clickable. This means we ignore the start and end markers.
     */

    /* Remember the original handler, so that we can restore it */
    char *original[MOUSE_BTN_COUNT];
    for (size_t i = 0; i < MOUSE_BTN_COUNT; i++)
        original[i] = exposable->on_click[i];

    if (event == ON_MOUSE_CLICK) {
        long where = clickable_width > 0
            ? 100 * (x - x_offset) / clickable_width
            : 0;

        struct tag_set tags = {
            .tags = (struct tag *[]){tag_new_int(NULL, "where", where)},
            .count = 1,
        };

        tags_expand_templates(
            exposable->on_click, (const char **)exposable->on_click,
            MOUSE_BTN_COUNT, &tags);
        tag_set_destroy(&tags);
    }

    /* Call default implementation, which will execute our handler */
    exposable_default_on_mouse(exposable, bar, event, btn, x, y);

    if (event == ON_MOUSE_CLICK) {
        /* Reset handler string */
        for (size_t i = 0; i < MOUSE_BTN_COUNT; i++) {
            free(exposable->on_click[i]);
            exposable->on_click[i] = original[i];
        }
    }
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    const struct tag *tag = tag_for_name(tags, p->tag);

    long value = tag != NULL ? tag->as_int(tag) : 0;
    long min = tag != NULL ? tag->min(tag) : 0;
    long max = tag != NULL ? tag->max(tag) : 0;

    LOG_DBG("%s: value=%ld, min=%ld, max=%ld",
            tag != NULL ? tag->name(tag) : "<no tag>", value, min, max);

    long fill_count = max == min ? 0 : p->width * value / (max - min);
    long empty_count = p->width - fill_count;

    struct eprivate *epriv = calloc(1, sizeof(*epriv));
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
    for (size_t i = 0; i < epriv->count; i++)
        assert(epriv->exposables[i] != NULL);

    struct exposable *exposable = exposable_common_new(particle, tags);

    exposable->private = epriv;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    exposable->on_mouse = &on_mouse;

    if (tag == NULL)
        return exposable;

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
    struct private *priv = calloc(1, sizeof(*priv));
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

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
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
        .font_shaping = common->font_shaping,
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

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"tag", true, &conf_verify_string},
        {"length", true, &conf_verify_unsigned},
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

const struct particle_iface particle_progress_bar_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_progress_bar_iface")));
#endif
