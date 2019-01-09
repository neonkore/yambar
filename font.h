#pragma once

#include <stdbool.h>
#include <cairo.h>

#include "color.h"

struct font;

struct font *font_new(const char *name);
struct font *font_clone(const struct font *font);
void font_destroy(struct font *font);

const char *font_face(const struct font *font);
cairo_scaled_font_t *font_scaled_font(const struct font *font);
