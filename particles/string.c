#include "string.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct private {
    char *text;

    struct font *font;
    struct rgba foreground;
};

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

static void
exposable_destroy(struct exposable *exposable)
{
    struct private *e = exposable->private;
    free(e->text);
    free(e);
    free(exposable);
}

struct sbuf {
    char *s;
    size_t size;
    size_t len;
};

static void
sbuf_strncat(struct sbuf *s1, const char *s2, size_t n)
{
    size_t s2_actual_len = strlen(s2);
    size_t s2_len = s2_actual_len < n ? s2_actual_len : n;

    if (s1->len + s2_len >= s1->size) {
        size_t required_size = s1->len + s2_len + 1;
        s1->size = 2 * required_size;

        s1->s = realloc(s1->s, s1->size);
        s1->s[s1->len] = '\0';
    }

    strncat(s1->s, s2, s2_len);
    s1->len += s2_len;
}

static void
sbuf_strcat(struct sbuf *s1, const char *s2)
{
    sbuf_strncat(s1, s2, strlen(s2));
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct private *e = malloc(sizeof(*e));

    struct sbuf formatted = {0};
    const char *src = p->text;

    while (true) {
        /* Find next tag opening '{' */
        const char *begin = strchr(src, '{');

        if (begin == NULL) {
            /* No more tags, copy remaining characters */
            sbuf_strcat(&formatted, src);
            break;
        }

        /* Find closing '}' */
        const char *end = strchr(begin, '}');
        if (end == NULL) {
            /* Wasn't actually a tag, copy as-is instead */
            sbuf_strncat(&formatted, src, begin - src + 1);
            src = begin + 1;
            continue;
        }

        /* Extract tag name */
        char tag_name[end - begin];
        strncpy(tag_name, begin + 1, end - begin - 1);
        tag_name[end - begin - 1] = '\0';

        /* Lookup tag */
        const struct tag *tag = tag_for_name(tags, tag_name);
        if (tag == NULL) {
            /* No such tag, copy as-is instead */
            sbuf_strncat(&formatted, src, begin - src + 1);
            src = begin + 1;
            continue;
        }

        /* Copy characters preceeding the tag (name) */
        sbuf_strncat(&formatted, src, begin - src);

        /* Copy tag value */
        const char *value = tag->as_string(tag);
        sbuf_strcat(&formatted, value);

        /* Skip past tag name + closing '}' */
        src = end + 1;
    }

    e->text = formatted.s;
    e->font = p->font;
    e->foreground = p->foreground;

    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->particle = particle;
    exposable->private = e;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    return exposable;
}

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;
    font_destroy(p->font);
    free(p->text);
    free(p);
    free(particle);
}

struct particle *
particle_string_new(const char *text, struct font *font,
                    struct rgba foreground, int left_margin, int right_margin)
{
    struct private *p = malloc(sizeof(*p));
    p->text = strdup(text);
    p->font = font;
    p->foreground = foreground;

    struct particle *particle = particle_common_new(left_margin, right_margin);

    particle->private = p;
    particle->destroy = &particle_destroy;
    particle->instantiate = &instantiate;

    return particle;
}
