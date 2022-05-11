#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define LOG_MODULE "map"
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particle.h"
#include "../plugin.h"
#include "dynlist.h"

#include "map.h"

bool
int_condition(const long tag_value, const long cond_value, enum map_op op)
{
    switch (op) {
    case MAP_OP_EQ: return tag_value == cond_value;
    case MAP_OP_NE: return tag_value != cond_value;
    case MAP_OP_LE: return tag_value <= cond_value;
    case MAP_OP_LT: return tag_value < cond_value;
    case MAP_OP_GE: return tag_value >= cond_value;
    case MAP_OP_GT: return tag_value > cond_value;
    case MAP_OP_SELF: LOG_WARN("using int tag as bool");
    default: return false;
    }
}

bool
float_condition(const double tag_value, const double cond_value, enum map_op op)
{
    switch (op) {
    case MAP_OP_EQ: return tag_value == cond_value;
    case MAP_OP_NE: return tag_value != cond_value;
    case MAP_OP_LE: return tag_value <= cond_value;
    case MAP_OP_LT: return tag_value < cond_value;
    case MAP_OP_GE: return tag_value >= cond_value;
    case MAP_OP_GT: return tag_value > cond_value;
    case MAP_OP_SELF: LOG_WARN("using float tag as bool");
    default: return false;
    }
}

bool
str_condition(const char* tag_value, const char* cond_value, enum map_op op)
{
    switch (op) {
    case MAP_OP_EQ: return strcmp(tag_value, cond_value) == 0;
    case MAP_OP_NE: return strcmp(tag_value, cond_value) != 0;
    case MAP_OP_LE: return strcmp(tag_value, cond_value) <= 0;
    case MAP_OP_LT: return strcmp(tag_value, cond_value) < 0;
    case MAP_OP_GE: return strcmp(tag_value, cond_value) >= 0;
    case MAP_OP_GT: return strcmp(tag_value, cond_value) > 0;
    case MAP_OP_SELF: LOG_WARN("using String tag as bool");
    default: return false;
    }
}

bool
eval_comparison(const struct map_condition* map_cond, const struct tag_set *tags)
{
    const struct tag *tag = tag_for_name(tags, map_cond->tag);
    if (tag == NULL) {
        LOG_WARN("tag %s not found", map_cond->tag);
        return false;
    }

    switch (tag->type(tag)) {
    case TAG_TYPE_INT: {
        errno = 0;
        char *end;
        const long cond_value = strtol(map_cond->value, &end, 0);

        if (errno == ERANGE) {
            LOG_WARN("value %s is too large", map_cond->value);
            return false;
        } else if (*end != '\0') {
            LOG_WARN("failed to parse %s into int", map_cond->value);
            return false;
        }

        const long tag_value = tag->as_int(tag);
        return int_condition(tag_value, cond_value, map_cond->op);
    }
    case TAG_TYPE_FLOAT: {
        errno = 0;
        char *end;
        const double cond_value = strtod(map_cond->value, &end);

        if (errno == ERANGE) {
            LOG_WARN("value %s is too large", map_cond->value);
            return false;
        } else if (*end != '\0') {
            LOG_WARN("failed to parse %s into float", map_cond->value);
            return false;
        }

        const double tag_value = tag->as_float(tag);
        return float_condition(tag_value, cond_value, map_cond->op);
    }
    case TAG_TYPE_BOOL:
        if (map_cond->op == MAP_OP_SELF)
            return tag->as_bool(tag);
        else {
            LOG_WARN("boolean tag '%s' should be used directly", map_cond->tag);
            return false;
        }
    case TAG_TYPE_STRING: {
        const char* tag_value = tag->as_string(tag);
        return str_condition(tag_value, map_cond->value, map_cond->op);
    }
    }
    return false;
}

bool
eval_map_condition(const struct map_condition* map_cond, const struct tag_set *tags)
{
    switch(map_cond->op)
    {
    case MAP_OP_NOT: return !eval_map_condition(map_cond->cond1, tags);
    case MAP_OP_AND: return eval_map_condition(map_cond->cond1, tags) && eval_map_condition(map_cond->cond2, tags);
    case MAP_OP_OR: return eval_map_condition(map_cond->cond1, tags) || eval_map_condition(map_cond->cond2, tags);
    default: return eval_comparison(map_cond, tags);
    }
}

void
free_map_condition(struct map_condition* c)
{
    switch (c->op)
    {
        case MAP_OP_EQ:
        case MAP_OP_NE:
        case MAP_OP_LE:
        case MAP_OP_LT:
        case MAP_OP_GE:
        case MAP_OP_GT:
            free(c->value);
            /* FALLTHROUGH */
        case MAP_OP_SELF:
            free(c->tag);
            break;
        case MAP_OP_AND:
        case MAP_OP_OR:
            free_map_condition(c->cond2);
            /* FALLTHROUGH */
        case MAP_OP_NOT:
            free_map_condition(c->cond1);
            break;
    }
    free(c);
}

struct particle_map {
    struct map_condition *condition;
    struct particle *particle;
};

struct private {
    struct particle *default_particle;
    struct particle_map *map;
    size_t count;
};

struct eprivate {
    struct exposable *exposable;
};

static void
exposable_destroy(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;
    e->exposable->destroy(e->exposable);

    free(e);
    exposable_default_destroy(exposable);
}

static int
begin_expose(struct exposable *exposable)
{
    struct eprivate *e = exposable->private;

    int width = e->exposable->begin_expose(e->exposable);
    assert(width >= 0);

    if (width > 0)
        width += exposable->particle->left_margin + exposable->particle->right_margin;

    exposable->width = width;
    return exposable->width;
}

static void
expose(const struct exposable *exposable, pixman_image_t *pix, int x, int y, int height)
{
    struct eprivate *e = exposable->private;

    exposable_render_deco(exposable, pix, x, y, height);
    e->exposable->expose(
        e->exposable, pix, x + exposable->particle->left_margin, y, height);
}

static void
on_mouse(struct exposable *exposable, struct bar *bar, enum mouse_event event,
         enum mouse_button btn, int x, int y)
{
    const struct particle *p = exposable->particle;
    const struct eprivate *e = exposable->private;

    if ((event == ON_MOUSE_MOTION &&
         exposable->particle->have_on_click_template) ||
        exposable->on_click[btn] != NULL)
    {
        /* We have our own handler */
        exposable_default_on_mouse(exposable, bar, event, btn, x, y);
        return;
    }

    int px = p->left_margin;
    if (x >= px && x < px + e->exposable->width) {
        if (e->exposable->on_mouse != NULL)
            e->exposable->on_mouse(e->exposable, bar, event, btn, x - px, y);
        return;
    }

    /* In the left- or right margin */
    exposable_default_on_mouse(exposable, bar, event, btn, x, y);
}

static struct exposable *
instantiate(const struct particle *particle, const struct tag_set *tags)
{
    const struct private *p = particle->private;
    struct particle *pp = NULL;

    for (size_t i = 0; i < p->count; i++) {
        const struct particle_map *e = &p->map[i];

        if (!eval_map_condition(e->condition, tags))
            continue;

        pp = e->particle;
        break;
    }

    struct eprivate *e = calloc(1, sizeof(*e));

    if (pp != NULL)
        e->exposable = pp->instantiate(pp, tags);
    else if (p->default_particle != NULL)
        e->exposable = p->default_particle->instantiate(p->default_particle, tags);
    else
        e->exposable = dynlist_exposable_new(NULL, 0, 0, 0);

    assert(e->exposable != NULL);

    struct exposable *exposable = exposable_common_new(particle, tags);
    exposable->private = e;
    exposable->destroy = &exposable_destroy;
    exposable->begin_expose = &begin_expose;
    exposable->expose = &expose;
    exposable->on_mouse = &on_mouse;
    return exposable;
}

static void
particle_destroy(struct particle *particle)
{
    struct private *p = particle->private;

    if (p->default_particle != NULL)
        p->default_particle->destroy(p->default_particle);

    for (size_t i = 0; i < p->count; i++) {
        struct particle *pp = p->map[i].particle;
        pp->destroy(pp);
        free_map_condition(p->map[i].condition);
    }

    free(p->map);
    free(p);
    particle_default_destroy(particle);
}

static struct particle *
map_new(struct particle *common, const struct particle_map particle_map[],
        size_t count, struct particle *default_particle)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->default_particle = default_particle;
    priv->count = count;
    priv->map = malloc(count * sizeof(priv->map[0]));

    for (size_t i = 0; i < count; i++) {
        priv->map[i].condition = particle_map[i].condition;
        priv->map[i].particle = particle_map[i].particle;
    }

    common->private = priv;
    common->destroy = &particle_destroy;
    common->instantiate = &instantiate;
    return common;
}


static bool
verify_map_conditions(keychain_t *chain, const struct yml_node *node)
{
    if (!yml_is_dict(node)) {
        LOG_ERR(
            "%s: must be a dictionary of workspace-name: particle mappings",
            conf_err_prefix(chain, node));
        return false;
    }

    bool result = true;
    for (struct yml_dict_iter it = yml_dict_iter(node);
         it.key != NULL;
         yml_dict_next(&it))
    {
        const char *key = yml_value_as_string(it.key);
        if (key == NULL) {
            LOG_ERR("%s: key must be a string", conf_err_prefix(chain, it.key));
            return false;
        }
        char *key_clone = strdup(key);
        YY_BUFFER_STATE buffer = yy_scan_string(key_clone);
        if (yyparse() != 0) {
            LOG_ERR("%s: %s", conf_err_prefix(chain, it.key), MAP_PARSER_ERROR_MSG);
            free(MAP_PARSER_ERROR_MSG);
            result = false;
        } else
            free_map_condition(MAP_CONDITION_PARSE_RESULT);
        yy_delete_buffer(buffer);
        free(key_clone);

        if (!conf_verify_particle(chain_push(chain, key), it.value))
            return false;

        chain_pop(chain);
    }

    return result;
}

static struct particle *
from_conf(const struct yml_node *node, struct particle *common)
{
    const struct yml_node *conditions = yml_get_value(node, "conditions");
    const struct yml_node *def = yml_get_value(node, "default");

    struct particle_map particle_map[yml_dict_length(conditions)];

    struct conf_inherit inherited = {
        .font = common->font,
        .font_shaping = common->font_shaping,
        .foreground = common->foreground
    };

    size_t idx = 0;
    for (struct yml_dict_iter it = yml_dict_iter(conditions);
         it.key != NULL;
         yml_dict_next(&it), idx++)
    {
        /* Note we can skip the error checking here */
        char *key_clone = strdup(yml_value_as_string(it.key));
        YY_BUFFER_STATE buffer = yy_scan_string(key_clone);
        yyparse();
        particle_map[idx].condition = MAP_CONDITION_PARSE_RESULT;
        yy_delete_buffer(buffer);
        free(key_clone);
        particle_map[idx].particle = conf_to_particle(it.value, inherited);
    }

    struct particle *default_particle = def != NULL
        ? conf_to_particle(def, inherited) : NULL;

    return map_new(common, particle_map, yml_dict_length(conditions), default_particle);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"conditions", true, &verify_map_conditions},
        {"default", false, &conf_verify_particle},
        PARTICLE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct particle_iface particle_map_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct particle_iface iface __attribute__((weak, alias("particle_map_iface")));
#endif
