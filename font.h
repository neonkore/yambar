#pragma once

#include <stdbool.h>
#include <cairo.h>

#include "color.h"

struct font;

struct font *font_new(
    const char *face, int size, bool italic, bool bold, int y_offset);

struct font *font_clone(const struct font *font);
void font_destroy(struct font *font);

const char *font_face(const struct font *font);
int font_size(const struct font *font);
bool font_is_italic(const struct font *font);
bool font_is_bold(const struct font *font);
int font_y_offset(const struct font *font);

cairo_scaled_font_t *font_use_in_cairo(const struct font *font, cairo_t *cr);
