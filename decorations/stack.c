#include <stdlib.h>

#define LOG_MODULE "stack"
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../decoration.h"
#include "../plugin.h"

struct private {
    struct deco **decos;
    size_t count;
};

static void
destroy(struct deco *deco)
{
    struct private *d = deco->private;
    for (size_t i = 0; i < d->count; i++)
        d->decos[i]->destroy(d->decos[i]);
    free(d->decos);
    free(d);
    free(deco);
}

static void
expose(const struct deco *deco, pixman_image_t *pix, int x, int y, int width, int height)
{
    const struct private *d = deco->private;
    for (size_t i = 0; i < d->count; i++)
        d->decos[i]->expose(d->decos[i], pix, x, y, width, height);
}

static struct deco *
stack_new(struct deco *decos[], size_t count)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->decos = malloc(count * sizeof(priv->decos[0]));
    priv->count = count;

    for (size_t i = 0; i < count; i++)
        priv->decos[i] = decos[i];

    struct deco *deco = calloc(1, sizeof(*deco));
    deco->private = priv;
    deco->expose = &expose;
    deco->destroy = &destroy;

    return deco;
}

static struct deco *
from_conf(const struct yml_node *node)
{
    size_t count = yml_list_length(node);

    struct deco *decos[count];
    size_t idx = 0;

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it), idx++)
    {
        decos[idx] = conf_to_deco(it.node);
    }

    return stack_new(decos, count);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: must be a list of decorations", conf_err_prefix(chain, node));
        return false;
    }

    for (struct yml_list_iter it = yml_list_iter(node);
         it.node != NULL;
         yml_list_next(&it))
    {
        if (!conf_verify_decoration(chain, it.node))
            return false;
    }

    return true;
}

const struct deco_iface deco_stack_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct deco_iface iface __attribute__((weak, alias("deco_stack_iface")));
#endif
