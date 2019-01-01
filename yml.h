#pragma once
#include <stdio.h>
#include <stdbool.h>

struct yml_node;

struct yml_node *yml_load(FILE *yml, char **error);
void yml_destroy(struct yml_node *root);

bool yml_is_scalar(const struct yml_node *node);
bool yml_is_dict(const struct yml_node *node);
bool yml_is_list(const struct yml_node *node);

const struct yml_node *yml_get_value(
    const struct yml_node *node, const char *path);

struct yml_list_iter {
    const struct yml_node *node;
    const void *private;
};
struct yml_list_iter yml_list_iter(const struct yml_node *list);
void yml_list_next(struct yml_list_iter *iter);
size_t yml_list_length(const struct yml_node *list);

struct yml_dict_iter {
    const struct yml_node *key;
    const struct yml_node *value;
    const void *private1;
    const void *private2;
};
struct yml_dict_iter yml_dict_iter(const struct yml_node *dict);
void yml_dict_next(struct yml_dict_iter *iter);
size_t yml_dict_length(const struct yml_node *dict);

const char *yml_value_as_string(const struct yml_node *value);
long yml_value_as_int(const struct yml_node *value);
bool yml_value_as_bool(const struct yml_node *value);

/* For debugging, prints on stdout */
void print_node(const struct yml_node *n);
