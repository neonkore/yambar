#include <stdlib.h>

#include "../config.h"
#include "../config-verify.h"
#include "../decoration.h"
#include "../plugin.h"

#define LOG_MODULE "border"
#define LOG_ENABLE_DBG 0
#include "../log.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

struct private {
    pixman_color_t color;
    int size;
};

static void
destroy(struct deco *deco)
{
    struct private *d = deco->private;
    free(d);
    free(deco);
}

static void
expose(const struct deco *deco, pixman_image_t *pix, int x, int y, int width, int height)
{
    const struct private *d = deco->private;
    pixman_image_fill_rectangles(
        PIXMAN_OP_OVER, pix, &d->color, 4,
        (pixman_rectangle16_t []){
            /* Top */
            {x, y, width, min(d->size, height)},

            /* Bottom */
            {x, max(y + height - d->size, y), width, min(d->size, height)},

            /* Left */
            {x, y, min(d->size, width), height},

            /* Right */
            {max(x + width - d->size, x), y, min(d->size, width), height},
        });
}

static struct deco *
border_new(pixman_color_t color, int size)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->color = color;
    priv->size = size;

    struct deco *deco = calloc(1, sizeof(*deco));
    deco->private = priv;
    deco->expose = &expose;
    deco->destroy = &destroy;

    return deco;
}

static struct deco *
from_conf(const struct yml_node *node)
{
    const struct yml_node *color = yml_get_value(node, "color");
    const struct yml_node *size = yml_get_value(node, "size");
    return border_new(
        conf_to_color(color),
        size != NULL ? yml_value_as_int(size) : 1);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"color", true, &conf_verify_color},
        {"size", false, &conf_verify_int},
        DECORATION_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct deco_iface deco_border_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct deco_iface iface __attribute__((weak, alias("deco_border_iface")));
#endif
