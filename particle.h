#pragma once
#include <cairo.h>

#include "color.h"
#include "config-verify.h"
#include "decoration.h"
#include "font.h"
#include "tag.h"
#include "yml.h"

struct bar;
struct particle;
struct exposable;

struct particle_info {
    bool (*verify_conf)(keychain_t *chain, const struct yml_node *node);
    struct particle *(*from_conf)(const struct yml_node *node, struct particle *common);

#define PARTICLE_COMMON_ATTRS_COUNT 5
#define PARTICLE_COMMON_ATTRS                      \
    {"margin", false, &conf_verify_int},           \
    {"left-margin", false, &conf_verify_int},      \
    {"right-margin", false, &conf_verify_int},     \
    {"on-click", false, &conf_verify_string},      \
    {"font", false, &conf_verify_font},            \
    {"foreground", false, &conf_verify_color},     \
    {"deco", false, &conf_verify_decoration},      \
    {NULL, false, NULL}

};

struct particle {
    void *private;

    int left_margin, right_margin;
    char *on_click_template;

    struct rgba foreground;
    struct font *font;
    struct deco *deco;

    void (*destroy)(struct particle *particle);
    struct exposable *(*instantiate)(const struct particle *particle,
                                     const struct tag_set *tags);
};

enum mouse_event {
    ON_MOUSE_MOTION,
    ON_MOUSE_CLICK,
};

struct exposable {
    const struct particle *particle;
    void *private;

    int width; /* Should be set by begin_expose(), at latest */
    char *on_click;

    void (*destroy)(struct exposable *exposable);
    int (*begin_expose)(struct exposable *exposable);
    void (*expose)(const struct exposable *exposable, cairo_t *cr,
                   int x, int y, int height);

    void (*on_mouse)(struct exposable *exposable, struct bar *bar,
                     enum mouse_event event, int x, int y);
};

struct particle *particle_common_new(
    int left_margin, int right_margin, const char *on_click_template,
    struct font *font, struct rgba foreground, struct deco *deco);

void particle_default_destroy(struct particle *particle);

struct exposable *exposable_common_new(
    const struct particle *particle, const char *on_click);
void exposable_default_destroy(struct exposable *exposable);
void exposable_render_deco(
    const struct exposable *exposable, cairo_t *cr, int x, int y, int height);

void exposable_default_on_mouse(
    struct exposable *exposable, struct bar *bar,
    enum mouse_event event, int x, int y);
