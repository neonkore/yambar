#include <stdlib.h>

#include "../config.h"
#include "../config-verify.h"
#include "../decoration.h"
#include "../plugin.h"

struct private {
    //struct rgba color;
    pixman_color_t color;
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
        PIXMAN_OP_OVER, pix, &d->color, 1,
        &(pixman_rectangle16_t){x, y, width, height});
}

static struct deco *
background_new(pixman_color_t color)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->color = color;

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
    return background_new(conf_to_color(color));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"color", true, &conf_verify_color},
        DECORATION_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct deco_iface deco_background_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct deco_iface iface __attribute__((weak, alias("deco_background_iface")));
#endif
