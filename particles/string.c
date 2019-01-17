#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_MODULE "string"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"

struct private {
    char *text;
    size_t max_len;
};

struct eprivate {
    char *text;
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

    cairo_scaled_font_t *scaled = font_scaled_font(exposable->particle->font);

    cairo_glyph_t *glyphs = NULL;
    int num_glyphs;
    cairo_status_t status = cairo_scaled_font_text_to_glyphs(
        scaled, 0, 0, e->text, -1, &glyphs, &num_glyphs, NULL, NULL, NULL);

    if (status != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("failed to convert \"%s\" to glyphs: %s",
                 e->text, cairo_status_to_string(status));

        memset(&e->extents, 0, sizeof(e->extents));
        exposable->width = 0;
    } else {
        cairo_scaled_font_glyph_extents(scaled, glyphs, num_glyphs, &e->extents);
        cairo_glyph_free(glyphs);

        exposable->width = (exposable->particle->left_margin +
                            e->extents.x_advance +
                            exposable->particle->right_margin);
    }

    return exposable->width;
}

static void
expose(const struct exposable *exposable, cairo_t *cr, int x, int y, int height)
{
    exposable_render_deco(exposable, cr, x, y, height);

    const struct eprivate *e = exposable->private;
    const size_t text_len = strlen(e->text);

    cairo_scaled_font_t *scaled = font_scaled_font(exposable->particle->font);

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs, num_clusters;

    cairo_status_t status = cairo_scaled_font_text_to_glyphs(
        scaled,
        x + exposable->particle->left_margin,
        (double)y + ((double)height - e->extents.height) / 2 - e->extents.y_bearing,
        e->text, text_len, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);

    if (status == CAIRO_STATUS_SUCCESS) {
        cairo_set_scaled_font(cr, scaled);
        cairo_set_source_rgba(cr,
                              exposable->particle->foreground.red,
                              exposable->particle->foreground.green,
                              exposable->particle->foreground.blue,
                              exposable->particle->foreground.alpha);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        cairo_show_text_glyphs(
            cr, e->text, text_len, glyphs, num_glyphs,
            clusters, num_clusters, cluster_flags);

        cairo_glyph_free(glyphs);
        cairo_text_cluster_free(clusters);
    }
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct eprivate *e = malloc(sizeof(*e));

    e->text = tags_expand_template(p->text, tags);
    memset(&e->extents, 0, sizeof(e->extents));

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
    struct private *p = malloc(sizeof(*p));
    p->text = strdup(text);
    p->max_len = max_len;

    common->private = p;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}

struct particle *
string_from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *text = yml_get_value(node, "text");
    const struct yml_node *max = yml_get_value(node, "max");

    return string_new(
        common,
        yml_value_as_string(text),
        max != NULL ? yml_value_as_int(max) : 0);
}

bool
string_verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"text", true, &conf_verify_string},
        {"max", false, &conf_verify_int},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

bool verify_conf(keychain_t *chain, const struct yml_node *node)
    __attribute__((weak, alias("string_verify_conf")));
struct deco *from_conf(const struct yml_node *node, struct particle *common)
    __attribute__((weak, alias("string_from_conf")));

#endif
