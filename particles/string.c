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

struct text_run_cache {
    uint64_t hash;
    struct fcft_text_run *run;
    int width;
    bool in_use;
};

struct private {
    char *text;
    size_t max_len;

    size_t cache_size;
    struct text_run_cache *cache;
};

struct eprivate {
    /* Set when instantiating */
    char *text;

    ssize_t cache_idx;
    const struct fcft_glyph **glyphs;
    const struct fcft_glyph **allocated_glyphs;
    long *kern_x;
    int num_glyphs;
};

static uint64_t
sdbm_hash(const char *s)
{
    uint64_t hash = 0;

    for (; *s != '\0'; s++) {
        int c = *s;
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;

    free(e->text);
    free(e->allocated_glyphs);
    free(e->kern_x);
    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    struct private *p = exposable->particle->private;
    struct fcft_font *font = exposable->particle->font;

    e->glyphs = e->allocated_glyphs = NULL;
    e->num_glyphs = 0;
    e->kern_x = NULL;
    e->cache_idx = -1;

    uint64_t hash = sdbm_hash(e->text);

    for (size_t i = 0; i < p->cache_size; i++) {
        if (p->cache[i].hash == hash) {
            assert(p->cache[i].run != NULL);

            p->cache[i].in_use = true;
            e->cache_idx = i;
            e->glyphs = p->cache[i].run->glyphs;
            e->num_glyphs = p->cache[i].run->count;
            e->kern_x = calloc(p->cache[i].run->count, sizeof(e->kern_x[0]));

            exposable->width =
                exposable->particle->left_margin +
                p->cache[i].width +
                exposable->particle->right_margin;

            return exposable->width;
        }
    }

    size_t chars = mbstowcs(NULL, e->text, 0);
    if (chars != (size_t)-1) {
        wchar_t wtext[chars + 1];
        mbstowcs(wtext, e->text, chars + 1);

        e->kern_x = calloc(chars, sizeof(e->kern_x[0]));

        if (fcft_capabilities() & FCFT_CAPABILITY_TEXT_RUN_SHAPING) {
            struct fcft_text_run *run = fcft_text_run_rasterize(font, chars, wtext, FCFT_SUBPIXEL_NONE);
            if (run != NULL) {
                int w = 0;
                for (size_t i = 0; i < run->count; i++)
                    w += run->glyphs[i]->advance.x;

                ssize_t cache_idx = -1;
                for (size_t i = 0; i < p->cache_size; i++) {
                    if (p->cache[i].run == NULL || !p->cache[i].in_use) {
                        fcft_text_run_destroy(p->cache[i].run);
                        cache_idx = i;
                        break;
                    }
                }

                if (cache_idx < 0) {
                    size_t new_size = p->cache_size + 1;
                    struct text_run_cache *new_cache = realloc(
                        p->cache, new_size * sizeof(new_cache[0]));

                    p->cache_size = new_size;
                    p->cache = new_cache;
                    cache_idx = new_size - 1;
                }

                assert(cache_idx >= 0 && cache_idx < p->cache_size);
                p->cache[cache_idx].hash = hash;
                p->cache[cache_idx].run = run;
                p->cache[cache_idx].width = w;
                p->cache[cache_idx].in_use = true;

                e->cache_idx = cache_idx;
                e->num_glyphs = run->count;
                e->glyphs = run->glyphs;
            }
        }

        if (e->glyphs == NULL) {
            e->allocated_glyphs = malloc(chars * sizeof(e->glyphs[0]));

            /* Convert text to glyph masks/images. */
            for (size_t i = 0; i < chars; i++) {
                const struct fcft_glyph *glyph = fcft_glyph_rasterize(
                    font, wtext[i], FCFT_SUBPIXEL_NONE);

                if (glyph == NULL)
                    continue;

                e->allocated_glyphs[e->num_glyphs++] = glyph;

                if (i == 0)
                    continue;

                fcft_kerning(font, wtext[i - 1], wtext[i], &e->kern_x[i], NULL);
            }

            e->glyphs = e->allocated_glyphs;
        }
    }

    exposable->width = exposable->particle->left_margin +
        exposable->particle->right_margin;

    /* Calculate the size we need to render the glyphs */
    for (int i = 0; i < e->num_glyphs; i++)
        exposable->width += e->kern_x[i] + e->glyphs[i]->advance.x;

    return exposable->width;
}

static void
expose(const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height)
{
    exposable_render_deco(exposable, pix, x, y, height);

    const struct eprivate *e = exposable->private;
    const struct fcft_font *font = exposable->particle->font;

    if (e->cache_idx >= 0) {
        struct private *priv = exposable->particle->private;
        priv->cache[e->cache_idx].in_use = false;
    }

    if (e->num_glyphs == 0)
        return;

    /*
     * This tries to center the font around the bar center, by using
     * the font's ascent+descent as total height, and then removing
     * its descent. This way, the part of the font *above* the
     * baseline is centered.
     *
     * "EEEE" will typically be dead center, with the middle of each character being in the bar's center. 
     * "eee" will be slightly below the center.
     * "jjj" will be even further below the center.
     *
     * Finally, if the font's descent is negative, ignore it (except
     * for the height calculation). This is unfortunately not based on
     * any real facts, but works very well with e.g. the "Awesome 5"
     * font family.
     */
    const double baseline = (double)y +
        (double)(height + font->ascent + font->descent) / 2.0 -
        (font->descent > 0 ? font->descent : 0);

    x += exposable->particle->left_margin;

    /* Loop glyphs and render them, one by one */
    for (int i = 0; i < e->num_glyphs; i++) {
        const struct fcft_glyph *glyph = e->glyphs[i];
        assert(glyph != NULL);

        x += e->kern_x[i];

        if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
            /* Glyph surface is a pre-rendered image (typically a color emoji...) */
            pixman_image_composite32(
                PIXMAN_OP_OVER, glyph->pix, NULL, pix, 0, 0, 0, 0,
                x + glyph->x, baseline - glyph->y,
                glyph->width, glyph->height);
        } else {
            /* Glyph surface is an alpha mask */
            pixman_image_t *src = pixman_image_create_solid_fill(&exposable->particle->foreground);
            pixman_image_composite32(
                PIXMAN_OP_OVER, src, glyph->pix, pix, 0, 0, 0, 0,
                x + glyph->x, baseline - glyph->y,
                glyph->width, glyph->height);
            pixman_image_unref(src);
        }

        x += glyph->advance.x;
    }
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct eprivate *e = calloc(1, sizeof(*e));

    e->text = tags_expand_template(p->text, tags);
    e->glyphs = NULL;
    e->num_glyphs = 0;

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
    for (size_t i = 0; i < p->cache_size; i++)
        fcft_text_run_destroy(p->cache[i].run);
    free(p->cache);
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
    p->cache_size = 0;
    p->cache = NULL;

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
