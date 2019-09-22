#pragma once

#include <stdbool.h>
#include <threads.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include <fontconfig/fontconfig.h>
#include <pixman.h>

#include "tllist.h"

struct glyph {
    wchar_t wc;
    int cols;

    pixman_image_t *pix;
    int x;
    int y;
    int x_advance;
    int width;
    int height;

    bool valid;
};

typedef tll(struct glyph) hash_entry_t;

struct font {
    char *name;

    FcPattern *fc_pattern;
    FcFontSet *fc_fonts;
    int fc_idx;

    FT_Face face;
    int load_flags;
    int render_flags;
    FT_LcdFilter lcd_filter;
    double pixel_size_fixup; /* Scale factor - should only be used with ARGB32 glyphs */
    bool bgr;  /* True for FC_RGBA_BGR and FC_RGBA_VBGR */

    struct {
        int height;
        int descent;
        int ascent;
        int max_x_advance;
    } fextents;

    struct {
        int position;
        int thickness;
    } underline;

    struct {
        int position;
        int thickness;
    } strikeout;

    hash_entry_t **cache;

    bool is_fallback;
    int ref_counter;
};

struct font *font_from_name(const char *name);
struct font *font_clone(const struct font *font);
const struct glyph *font_glyph_for_wc(struct font *font, wchar_t wc);
void font_destroy(struct font *font);
