#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "string"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"

struct private {
    char *text;
    size_t max_len;
};

struct eprivate {
    /* Set when instantiating */
    char *text;

    /* Set in begin_expose() */
    cairo_font_extents_t fextents;
    cairo_glyph_t *glyphs;
    cairo_text_cluster_t *clusters;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs;
    int num_clusters;
};

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;

    free(e->text);
    if (e->glyphs != NULL)
        cairo_glyph_free(e->glyphs);
    if (e->clusters != NULL)
        cairo_text_cluster_free(e->clusters);

    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    struct eprivate *e = exposable->private;

    cairo_scaled_font_t *scaled = font_scaled_font(exposable->particle->font);

    cairo_set_scaled_font(cr, scaled);
    cairo_font_extents(cr, &e->fextents);

    LOG_DBG("%s: ascent=%f, descent=%f, height=%f",
            font_face(exposable->particle->font),
            e->fextents.ascent, e->fextents.descent, e->fextents.height);

    cairo_status_t status = cairo_scaled_font_text_to_glyphs(
        scaled, 0, 0, e->text, -1, &e->glyphs, &e->num_glyphs,
        &e->clusters, &e->num_clusters, &e->cluster_flags);

    if (status != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("failed to convert \"%s\" to glyphs: %s",
                 e->text, cairo_status_to_string(status));

        e->num_glyphs = -1;
        e->num_clusters = -1;
        memset(&e->fextents, 0, sizeof(e->fextents));
        exposable->width = 0;
    } else {
        cairo_text_extents_t extents;
        cairo_scaled_font_glyph_extents(
            scaled, e->glyphs, e->num_glyphs, &extents);

        exposable->width = (exposable->particle->left_margin +
                            extents.x_advance +
                            exposable->particle->right_margin);
    }

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    exposable_render_deco(exposable, cr, x, y, height);

    const struct eprivate *e = exposable->private;

    if (e->num_glyphs == -1)
        return;

    /* Adjust glyph offsets */
    for (int i = 0; i < e->num_glyphs; i++) {
        e->glyphs[i].x += x + exposable->particle->left_margin;
        e->glyphs[i].y += (double)y +
            (double)(e->fextents.height - e->fextents.descent + height) / 2.0;
        //(double)(e->fextents.ascent + e->fextents.descent + height) / 2.0;
    }

    cairo_scaled_font_t *scaled = font_scaled_font(exposable->particle->font);
    cairo_set_scaled_font(cr, scaled);
    cairo_set_source_rgba(cr,
                          exposable->particle->foreground.red,
                          exposable->particle->foreground.green,
                          exposable->particle->foreground.blue,
                          exposable->particle->foreground.alpha);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_show_text_glyphs(
        cr, e->text, -1, e->glyphs, e->num_glyphs,
        e->clusters, e->num_clusters, e->cluster_flags);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct eprivate *e = calloc(1, sizeof(*e));

    e->text = tags_expand_template(p->text, tags);
    e->num_glyphs = -1;
    e->num_clusters = -1;

    if (p->max_len > 0) {
        const size_t len = strlen(e->text);
        if (len > p->max_len) {

            size_t end = p->max_len;
            if (end >= 3) {
                /* "allocate" room for three dots at the end */
                end -= 3;
            }

            /* Mucho importante - don't cut in the middle of a utf8 multibyte */
            while (end > 0 && e->text[end - 1] >> 7)
                end--;

            if (p->max_len > 3) {
                for (size_t i = 0; i < 3; i++)
                    e->text[end + i] = '.';
                e->text[end + 3] = '\0';
            } else
                e->text[end] = '\0';
        }
    }

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
    free(p->text);
    free(p);
    particle_default_destroy(particle);
}

static struct particle *
string_new(struct particle *common, const char *text, size_t max_len)
{
    struct private *p = calloc(1, sizeof(*p));
    p->text = strdup(text);
    p->max_len = max_len;

    common->private = p;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *text = yml_get_value(node, "text");
    const struct yml_node *max = yml_get_value(node, "max");

    return string_new(
        common,
        yml_value_as_string(text),
        max != NULL ? yml_value_as_int(max) : 0);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"text", true, &conf_verify_string},
        {"max", false, &conf_verify_int},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct particle_iface particle_string_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_string_iface")));
#endif
