#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct font {
    char *face;
    int size;
    bool italic;
    bool bold;

    int y_offset;
    cairo_font_options_t *cairo_font_options;
};

struct font *
font_new(const char *face, int size, bool italic, bool bold, int y_offset)
{
    struct font *font = malloc(sizeof(*font));

    font->face = strdup(face);
    font->size = size;
    font->italic = italic;
    font->bold = bold;
    font->y_offset = y_offset;

    font->cairo_font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(
        font->cairo_font_options, CAIRO_ANTIALIAS_DEFAULT);
    //antialias ? CAIRO_ANTIALIAS_SUBPIXEL : CAIRO_ANTIALIAS_NONE);

    return font;
}

struct font *
font_clone(const struct font *font)
{
    return font_new(font->face, font->size, font->italic, font->bold, font->y_offset);
}

void
font_destroy(struct font *font)
{
    cairo_font_options_destroy(font->cairo_font_options);
    free(font->face);
    free(font);
}

const char *
font_face(const struct font *font)
{
    return font->face;
}

int
font_size(const struct font *font)
{
    return font->size;
}

bool
font_is_italic(const struct font *font)
{
    return font->italic;
}

bool
font_is_bold(const struct font *font)
{
    return font->bold;
}

int
font_y_offset(const struct font *font)
{
    return font->y_offset;
}

cairo_scaled_font_t *
font_use_in_cairo(const struct font *font, cairo_t *cr)
{
    cairo_font_slant_t slant = font->italic
        ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL;
    cairo_font_weight_t weight = font->bold
        ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL;

    cairo_select_font_face(cr, font->face, slant, weight);
    cairo_set_font_size(cr, font->size);
    cairo_set_font_options(cr, font->cairo_font_options);

    return cairo_get_scaled_font(cr);
}
