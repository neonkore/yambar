#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <fontconfig/fontconfig.h>
#include <cairo-ft.h>

#define LOG_MODULE "font"
#define LOG_ENABLE_DBG 0
#include "log.h"

struct font {
    char *name;
    cairo_scaled_font_t *scaled_font;
};

static void __attribute__((constructor))
init(void)
{
    FcInit();

#if defined(LOG_ENABLE_DBG) && LOG_ENABLE_DBG
    int raw_version = FcGetVersion();

    /* See FC_VERSION in <fontconfig/fontconfig.h> */
    const int major = raw_version / 10000; raw_version %= 10000;
    const int minor = raw_version / 100; raw_version %= 100;
    const int patch = raw_version;
#endif

    LOG_DBG("fontconfig: %d.%d.%d", major, minor, patch);
}

static void __attribute__((destructor))
fini(void)
{
    FcFini();
}

struct font *
font_new(const char *name)
{
    FcPattern *pattern = FcNameParse((const unsigned char *)name);
    if (pattern == NULL) {
        LOG_ERR("%s: failed to lookup font", name);
        return NULL;
    }

    if (!FcConfigSubstitute(NULL, pattern, FcMatchPattern)) {
        LOG_ERR("%s: failed to do config substitution", name);
        FcPatternDestroy(pattern);
        return NULL;
    }

    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *final_pattern = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);

    if (final_pattern == NULL) {
        LOG_ERR("%s: failed to match font", name);
        return NULL;
    }

    double font_size;
    if (FcPatternGetDouble(final_pattern, FC_PIXEL_SIZE, 0, &font_size)) {
        LOG_ERR("%s: failed to get size", name);
        FcPatternDestroy(final_pattern);
        return NULL;
    }

    cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(
        final_pattern);

    FcPatternDestroy(final_pattern);

    if (cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("%s: failed to create cairo font face", name);
        cairo_font_face_destroy(face);
        return NULL;
    }

    cairo_matrix_t matrix, ctm;
    cairo_matrix_init_identity(&ctm);
    cairo_matrix_init_scale(&matrix, font_size, font_size);

    cairo_font_options_t *options = cairo_font_options_create();
    cairo_scaled_font_t *scaled_font = cairo_scaled_font_create(
        face, &matrix, &ctm, options);

    cairo_font_options_destroy(options);
    cairo_font_face_destroy(face);

    if (cairo_scaled_font_status(scaled_font) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("%s: failed to create scaled font", name);
        cairo_scaled_font_destroy(scaled_font);
        return NULL;
    }

    struct font *font = malloc(sizeof(*font));
    font->name = strdup(name);
    font->scaled_font = scaled_font;

    return font;
}

struct font *
font_clone(const struct font *font)
{
    struct font *clone = malloc(sizeof(*font));
    clone->name = strdup(font->name);
    clone->scaled_font = font->scaled_font;

    cairo_scaled_font_reference(clone->scaled_font);
    return clone;
}

void
font_destroy(struct font *font)
{
    if (font == NULL)
        return;

    cairo_scaled_font_destroy(font->scaled_font);
    free(font->name);
    free(font);
}

const char *
font_face(const struct font *font)
{
    return font->name;
}

cairo_scaled_font_t *
font_scaled_font(const struct font *font)
{
    return font->scaled_font;
}
