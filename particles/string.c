#include "string.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct private {
    char *text;

    struct font *font;
    struct rgba foreground;
};

static void
exposable_destroy(struct exposable *exposable)
{
    struct private *e = exposable->private;
    free(e->text);
    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable, cairo_t *cr)
{
    const struct private *e = exposable->private;

    cairo_scaled_font_t *scaled = font_use_in_cairo(e->font, cr);
    cairo_text_extents_t extents;
    cairo_scaled_font_text_extents(scaled, e->text, &extents);

    exposable->width = (exposable->particle->left_margin +
                        extents.x_advance +
                        exposable->particle->right_margin);

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    const struct private *e = exposable->private;

    cairo_scaled_font_t *scaled = font_use_in_cairo(e->font, cr);

    cairo_text_extents_t extents;
    cairo_scaled_font_text_extents(scaled, e->text, &extents);

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs, num_clusters;
    cairo_scaled_font_text_to_glyphs(
        scaled,
        x + exposable->particle->left_margin,
        (double)y + ((double)height - extents.y_bearing) / 2 + font_y_offset(e->font),
        e->text, strlen(e->text), &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);

    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, cr, x, y, exposable->width, height);

    cairo_set_source_rgba(cr,
                          e->foreground.red,
                          e->foreground.green,
                          e->foreground.blue,
                          e->foreground.alpha);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_show_text_glyphs(cr, e->text, strlen(e->text),
                           glyphs, num_glyphs,
                           clusters, num_clusters, cluster_flags);
    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);
    /*cairo_scaled_font_destroy(scaled);*/

}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct private *e = malloc(sizeof(*e));

    e->text = tags_expand_template(p->text, tags);
    e->font = p->font;
    e->foreground = p->foreground;

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

struct particle *
particle_string_new(const char *text, struct font *font,
                    struct rgba foreground, int left_margin, int right_margin,
                    const char *on_click_template)
{
    struct private *p = malloc(sizeof(*p));
    p->text = strdup(text);
    p->font = font;
    p->foreground = foreground;

    struct particle *particle = particle_common_new(
        left_margin, right_margin, on_click_template);

    particle->private = p;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    return particle;
}
