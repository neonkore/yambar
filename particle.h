#pragma once
#include <pixman.h>

#include <fcft/fcft.h>

#include "color.h"
#include "decoration.h"
#include "font-shaping.h"
#include "tag.h"

enum mouse_event {
    ON_MOUSE_MOTION,
    ON_MOUSE_CLICK,
};

enum mouse_button {
    MOUSE_BTN_NONE,
    MOUSE_BTN_LEFT,
    MOUSE_BTN_MIDDLE,
    MOUSE_BTN_RIGHT,
    MOUSE_BTN_WHEEL_UP,
    MOUSE_BTN_WHEEL_DOWN,

    MOUSE_BTN_COUNT,
};

struct bar;

struct particle {
    void *private;

    int left_margin, right_margin;

    bool have_on_click_template;
    char *on_click_templates[MOUSE_BTN_COUNT];

    pixman_color_t foreground;
    struct fcft_font *font;
    enum font_shaping font_shaping;
    struct deco *deco;

    void (*destroy)(struct particle *particle);
    struct exposable *(*instantiate)(const struct particle *particle,
                                     const struct tag_set *tags);
};


struct exposable {
    const struct particle *particle;
    void *private;

    int width; /* Should be set by begin_expose(), at latest */
    char *on_click[MOUSE_BTN_COUNT];

    void (*destroy)(struct exposable *exposable);
    int (*begin_expose)(struct exposable *exposable);
    void (*expose)(const struct exposable *exposable, pixman_image_t *pix,
                   int x, int y, int height);

    void (*on_mouse)(struct exposable *exposable, struct bar *bar,
                     enum mouse_event event, enum mouse_button btn, int x, int y);
};

struct particle *particle_common_new(
    int left_margin, int right_margin, const char *on_click_templates[],
    struct fcft_font *font, enum font_shaping font_shaping,
    pixman_color_t foreground, struct deco *deco);

void particle_default_destroy(struct particle *particle);

struct exposable *exposable_common_new(
    const struct particle *particle, const struct tag_set *tags);
void exposable_default_destroy(struct exposable *exposable);
void exposable_render_deco(
    const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height);

void exposable_default_on_mouse(
    struct exposable *exposable, struct bar *bar,
    enum mouse_event event, enum mouse_button btn, int x, int y);

/* List of attributes *all* particles implement */
#define PARTICLE_COMMON_ATTRS                           \
    {"margin", false, &conf_verify_unsigned},           \
    {"left-margin", false, &conf_verify_unsigned},      \
    {"right-margin", false, &conf_verify_unsigned},     \
    {"on-click", false, &conf_verify_on_click},         \
    {"font", false, &conf_verify_font},                 \
    {"font-shaping", false, &conf_verify_font_shaping}, \
    {"foreground", false, &conf_verify_color},          \
    {"deco", false, &conf_verify_decoration},           \
    {NULL, false, NULL}
