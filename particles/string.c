#include "string.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "string"
#define LOG_ENABLE_DBG 1
#include "../log.h"
#include "../config.h"

struct private {
    char *text;
    size_t max_len;

    struct font *font;
    struct rgba foreground;
};

struct eprivate {
    char *text;

    const struct font *font;
    struct rgba foreground;

    cairo_text_extents_t extents;
};

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    free(e->text);
    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;

    cairo_scaled_font_t *scaled = font_scaled_font(e->font);
    cairo_scaled_font_text_extents(scaled, e->text, &e->extents);

    exposable->width = (exposable->particle->left_margin +
                        e->extents.x_advance +
                        exposable->particle->right_margin);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    exposable_render_deco(exposable, cr, x, y, height);

    const struct eprivate *e = exposable->private;
    const size_t text_len = strlen(e->text);

    cairo_scaled_font_t *scaled = font_scaled_font(e->font);

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs, num_clusters;
    cairo_scaled_font_text_to_glyphs(
        scaled,
        x + exposable->particle->left_margin,
        (double)y + ((double)height - e->extents.height) / 2 - e->extents.y_bearing,
        e->text, text_len, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);

    cairo_set_scaled_font(cr, scaled);
    cairo_set_source_rgba(cr,
                          e->foreground.red,
                          e->foreground.green,
                          e->foreground.blue,
                          e->foreground.alpha);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_show_text_glyphs(
        cr, e->text, text_len, glyphs, num_glyphs,
        clusters, num_clusters, cluster_flags);

    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct eprivate *e = malloc(sizeof(*e));

    e->text = tags_expand_template(p->text, tags);
    e->font = p->font;
    e->foreground = p->foreground;
    memset(&e->extents, 0, sizeof(e->extents));

    if (p->max_len > 0) {
        size_t len = strlen(e->text);
        if (len > p->max_len) {
            if (p->max_len >= 3) {
                for (size_t i = 0; i < 3; i++)
                    e->text[p->max_len - 3 + i] = '.';
            }
            e->text[p->max_len] = '\0';
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
    font_destroy(p->font);
    free(p->text);
    free(p);
    particle_default_destroy(particle);
}

static struct particle *
string_new(const char *text, size_t max_len, struct font *font,
           struct rgba foreground, int left_margin, int right_margin,
           const char *on_click_template)
{
    struct private *p = malloc(sizeof(*p));
    p->text = strdup(text);
    p->max_len = max_len;
    p->font = font;
    p->foreground = foreground;

    struct particle *particle = particle_common_new(
        left_margin, right_margin, on_click_template);

    particle->private = p;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    return particle;
}

static struct particle *
from_conf(const struct yml_node *node, const struct font *parent_font,
          int left_margin, int right_margin, const char *on_click_template)
{
    const struct yml_node *text = yml_get_value(node, "text");
    const struct yml_node *max = yml_get_value(node, "max");
    const struct yml_node *font = yml_get_value(node, "font");
    const struct yml_node *foreground = yml_get_value(node, "foreground");

    struct rgba fg_color = foreground != NULL
        ? conf_to_color(foreground) : (struct rgba){1.0, 1.0, 1.0, 1.0};

    return string_new(
        yml_value_as_string(text),
        max != NULL ? yml_value_as_int(max) : 0,
        font != NULL ? conf_to_font(font) : font_clone(parent_font),
        fg_color, left_margin, right_margin, on_click_template);
}

const struct particle_info plugin_info = {
    .from_conf = &from_conf,
    .attr_count = PARTICLE_COMMON_ATTRS_COUNT + 5,
    .attrs = {
        {"text", true, &conf_verify_string},
        {"max", false, &conf_verify_int},
        {"font", false, &conf_verify_font},
        {"foreground", false, &conf_verify_color},
        {"deco", false, &conf_verify_decoration},
        PARTICLE_COMMON_ATTRS,
    },
};
