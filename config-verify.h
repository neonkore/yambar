#pragma once

#include <stdbool.h>

#include "tllist.h"
#include "yml.h"

typedef tll(const char *) keychain_t;

struct attr_info {
    const char *name;
    bool required;
    bool (*verify)(keychain_t *chain, const struct yml_node *node);
};

static inline keychain_t *
chain_push(keychain_t *chain, const char *key)
{
    tll_push_back(*chain, key);
    return chain;
}

static inline void
chain_pop(keychain_t *chain)
{
    tll_pop_back(*chain);
}

const char *conf_err_prefix(
    const keychain_t *chain, const struct yml_node *node);


bool conf_verify_string(keychain_t *chain, const struct yml_node *node);
bool conf_verify_int(keychain_t *chain, const struct yml_node *node);

bool conf_verify_enum(keychain_t *chain, const struct yml_node *node,
                      const char *values[], size_t count);
bool conf_verify_list(keychain_t *chain, const struct yml_node *node,
                      bool (*verify)(keychain_t *chain, const struct yml_node *node));
bool conf_verify_dict(keychain_t *chain, const struct yml_node *node,
                      const struct attr_info info[]); /* NULL-terminated list */

bool conf_verify_on_click(keychain_t *chain, const struct yml_node *node);
bool conf_verify_color(keychain_t *chain, const struct yml_node *node);
bool conf_verify_font(keychain_t *chain, const struct yml_node *node);

bool conf_verify_particle(keychain_t *chain, const struct yml_node *node);
bool conf_verify_particle_list_items(keychain_t *chain, const struct yml_node *node);

bool conf_verify_decoration(keychain_t *chain, const struct yml_node *node);
