#include "yml.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <yaml.h>

enum node_type {
    ROOT,
    SCALAR,
    DICT,
    LIST,
};

struct yml_node;
struct llist_element {
    struct yml_node *node;
    struct llist_element *prev;
    struct llist_element *next;
};

struct llist {
    struct llist_element *head;
    struct llist_element *tail;
};

struct anchor_map {
    char *anchor;
    const struct yml_node *node;
};

struct yml_node {
    enum node_type type;
    union {
        struct {
            struct yml_node *root;
            struct anchor_map anchors[10];
            size_t anchor_count;
        } root;
        struct {
            char *value;
        } scalar;
        struct {
            struct llist keys;
            struct llist values;
            size_t key_count;
            size_t value_count;
        } dict;
        struct {
            struct llist values;
        } list;
    };

    struct yml_node *parent;
};

static void
llist_add(struct llist *list, struct yml_node *node)
{
    struct llist_element *element = malloc(sizeof(*element));
    element->node = node;
    element->prev = NULL;
    element->next = NULL;

    if (list->tail == NULL) {
        assert(list->head == NULL);
        list->head = list->tail = element;
    } else {
        assert(list->head != NULL);
        list->tail->next = element;
        element->prev = list->tail;
    }

    list->tail = element;
}

static struct yml_node *
clone_node(struct yml_node *parent, const struct yml_node *node)
{
    struct yml_node *clone = calloc(1, sizeof(*clone));
    clone->type = node->type;
    clone->parent = parent;

    switch (node->type) {
    case SCALAR:
        clone->scalar.value = strdup(node->scalar.value);
        break;

    case DICT:
        for (const struct llist_element *k = node->dict.keys.head,
                 *v = node->dict.values.head;
             k != NULL;
             k = k->next, v = v->next)
        {
            llist_add(&clone->dict.keys, clone_node(clone, k->node));
            llist_add(&clone->dict.values, clone_node(clone, v->node));
        }

        clone->dict.key_count = node->dict.key_count;
        clone->dict.value_count = node->dict.value_count;
        break;

    case LIST:
        for (const struct llist_element *v = node->list.values.head;
             v != NULL;
             v = v->next)
        {
            llist_add(&clone->list.values, clone_node(clone, v->node));
        }
        break;

    case ROOT:
        assert(false);
        break;
    }

    return clone;
}

static void
add_node(struct yml_node *parent, struct yml_node *new_node)
{
    switch (parent->type) {
    case ROOT:
        assert(parent->root.root == NULL);
        parent->root.root = new_node;
        new_node->parent = parent;
        break;

    case DICT:
        if (parent->dict.key_count == parent->dict.value_count) {
            llist_add(&parent->dict.keys, new_node);
            parent->dict.key_count++;
        } else {
            llist_add(&parent->dict.values, new_node);
            parent->dict.value_count++;
        }
        new_node->parent = parent;
        break;

    case LIST:
        llist_add(&parent->list.values, new_node);
        new_node->parent = parent;
        break;

    case SCALAR:
        assert(false);
        break;
    }
}

static void
add_anchor(struct yml_node *root, const char *anchor,
           const struct yml_node *node)
{
    assert(root->type == ROOT);

    struct anchor_map *map = &root->root.anchors[root->root.anchor_count];
    map->anchor = strdup(anchor);
    map->node = node;
    root->root.anchor_count++;
}

static void
post_process(struct yml_node *node)
{
    switch (node->type) {
    case ROOT:
        post_process(node->root.root);
        break;

    case SCALAR:
        //assert(strcmp(node->scalar.value, "<<") != 0);
        break;

    case LIST:
        for (struct llist_element *v = node->list.values.head;
             v != NULL;
             v = v->next)
        {
            post_process(v->node);
        }
        break;

    case DICT:
        for (struct llist_element *k = node->dict.keys.head,
                 *v = node->dict.values.head;
             k != NULL;
             k = k->next, v = v->next)
        {
            post_process(k->node);
            post_process(v->node);
        }

        for (struct llist_element *k = node->dict.keys.head,
                 *v = node->dict.values.head,
                 *k_next = k ? k->next : NULL, *v_next = v ? v->next : NULL;
             k != NULL;
             k = k_next, v = v_next,
                 k_next = k_next ? k_next->next : NULL,
                 v_next = v_next ? v_next->next : NULL)
        {
            if (k->node->type != SCALAR)
                continue;

            if (strcmp(k->node->scalar.value, "<<") != 0)
                continue;

            assert(v->node->type == DICT);

            for (struct llist_element *mk = v->node->dict.keys.head,
                     *mv = v->node->dict.values.head,
                     *mk_next = mk ? mk->next : NULL,
                     *mv_next = mv ? mv->next : NULL;
                 mk != NULL;
                 mk = mk_next, mv = mv_next,
                     mk_next = mk_next ? mk_next->next : NULL,
                     mv_next = mv_next ? mv_next->next : NULL)
            {
                /* TODO: verify key doesn't already exist */
                llist_add(&node->dict.keys, mk->node);
                llist_add(&node->dict.values, mv->node);

                node->dict.key_count++;
                node->dict.value_count++;

                free(mk);
                free(mv);
            }

            /* Delete merge node */
            struct llist_element *prev_k = k->prev;
            struct llist_element *next_k = k->next;
            struct llist_element *prev_v = v->prev;
            struct llist_element *next_v = v->next;

            if (prev_k != NULL) {
                assert(node->dict.keys.head != k);
                prev_k->next = next_k;
                prev_v->next = next_v;
            } else {
                assert(node->dict.keys.head == k);
                node->dict.keys.head = next_k;
                node->dict.values.head = next_v;
            }

            if (next_k != NULL) {
                assert(node->dict.keys.tail != k);
                next_k->prev = prev_k;
                next_v->prev = prev_v;
            } else {
                assert(node->dict.keys.tail == k);
                node->dict.keys.tail = prev_k;
                node->dict.values.tail = prev_v;
            }

            v->node->dict.key_count = v->node->dict.value_count = 0;
            v->node->dict.keys.head = v->node->dict.keys.tail = NULL;
            v->node->dict.values.head = v->node->dict.values.tail = NULL;

            yml_destroy(k->node);
            yml_destroy(v->node);
            free(k);
            free(v);
        }
        break;
    }
}

struct yml_node *
yml_load(FILE *yml)
{
    yaml_parser_t yaml;
    yaml_parser_initialize(&yaml);

    //FILE *yml = fopen("yml.yml", "r");
    //assert(yml != NULL);

    yaml_parser_set_input_file(&yaml, yml);

    bool done = false;
    int indent = 0;

    struct yml_node *root = malloc(sizeof(*root));
    root->type = ROOT;
    root->root.root = NULL;
    root->root.anchor_count = 0;

    struct yml_node *n = root;

    while (!done) {
        yaml_event_t event;
        if (!yaml_parser_parse(&yaml, &event)) {
            //printf("yaml parser error\n");
            /* TODO: free node tree */
            root = NULL;
            assert(false);
            break;
        }

        switch (event.type) {
        case YAML_NO_EVENT:
            //printf("%*sNO EVENT\n", indent, "");
            break;

        case YAML_STREAM_START_EVENT:
            //printf("%*sSTREAM START\n", indent, "");
            indent += 2;
            break;

        case YAML_STREAM_END_EVENT:
            indent -= 2;
            //printf("%*sSTREAM END\n", indent, "");
            done = true;
            break;

        case YAML_DOCUMENT_START_EVENT:
            //printf("%*sDOC START\n", indent, "");
            indent += 2;
            break;

        case YAML_DOCUMENT_END_EVENT:
            indent -= 2;
            //printf("%*sDOC END\n", indent, "");
            break;

        case YAML_ALIAS_EVENT:
            //printf("%*sALIAS %s\n", indent, "", event.data.alias.anchor);

            for (size_t i = 0; i < root->root.anchor_count; i++) {
                const struct anchor_map *map = &root->root.anchors[i];

                if (strcmp(map->anchor, (const char *)event.data.alias.anchor) != 0)
                    continue;

                struct yml_node *clone = clone_node(NULL, map->node);
                assert(clone != NULL);
                add_node(n, clone);
                break;
            }
            break;

        case YAML_SCALAR_EVENT: {
            //printf("%*sSCALAR: %.*s\n", indent, "",
            //(int)event.data.scalar.length, event.data.scalar.value);
            struct yml_node *new_scalar = calloc(1, sizeof(*new_scalar));
            new_scalar->type = SCALAR;
            new_scalar->scalar.value = strndup(
                (const char*)event.data.scalar.value, event.data.scalar.length);
            add_node(n, new_scalar);

            if (event.data.scalar.anchor != NULL) {
                const char *anchor = (const char *)event.data.scalar.anchor;
                add_anchor(root, anchor, new_scalar);
            }

            break;
        }

        case YAML_SEQUENCE_START_EVENT: {
            //printf("%*sSEQ START\n", indent, "");
            indent += 2;
            struct yml_node *new_list = calloc(1, sizeof(*new_list));
            new_list->type = LIST;
            add_node(n, new_list);
            n = new_list;

            if (event.data.sequence_start.anchor != NULL) {
                const char *anchor = (const char *)event.data.sequence_start.anchor;
                add_anchor(root, anchor, new_list);
            }
            break;
        }

        case YAML_SEQUENCE_END_EVENT:
            indent -= 2;
            //printf("%*sSEQ END\n", indent, "");

            assert(n->parent != NULL);
            n = n->parent;
            break;

        case YAML_MAPPING_START_EVENT: {
            //printf("%*sMAP START\n", indent, "");
            indent += 2;

            struct yml_node *new_dict = calloc(1, sizeof(*new_dict));
            new_dict->type = DICT;
            add_node(n, new_dict);
            n = new_dict;

            if (event.data.mapping_start.anchor != NULL) {
                const char *anchor = (const char *)event.data.mapping_start.anchor;
                add_anchor(root, anchor, new_dict);
            }
            break;
        }

        case YAML_MAPPING_END_EVENT:
            indent -= 2;
            //printf("%*sMAP END\n", indent, "");
            assert(n->parent != NULL);
            n = n->parent;
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&yaml);

    print_node(root);
    post_process(root);
    print_node(root);
    return root;
}

void
yml_destroy(struct yml_node *node)
{
    switch (node->type) {
    case ROOT:
        yml_destroy(node->root.root);
        for (size_t i = 0; i < node->root.anchor_count; i++)
            free(node->root.anchors[i].anchor);
        break;

    case SCALAR:
        free(node->scalar.value);
        break;

    case LIST:
        for (struct llist_element *e = node->list.values.head,
                 *n = e ? e->next : NULL;
             e != NULL;
             e = n, n = n ? n->next : NULL)
        {
            yml_destroy(e->node);
            free(e);
        }
        break;

    case DICT:
        for (struct llist_element *key = node->dict.keys.head,
                 *value = node->dict.values.head,
                 *n_key = key ? key->next : NULL,
                 *n_value = value ? value->next : NULL;
             key != NULL;
             key = n_key, value = n_value,
                 n_key = n_key ? n_key->next : NULL,
                 n_value = n_value ? n_value->next : NULL)
        {
            yml_destroy(value->node);
            yml_destroy(key->node);

            free(key);
            free(value);
        }
        break;
    }

    free(node);
}

bool
yml_is_scalar(const struct yml_node *node)
{
    return node->type == SCALAR;
}

bool
yml_is_dict(const struct yml_node *node)
{
    return node->type == DICT;
}

bool
yml_is_list(const struct yml_node *node)
{
    return node->type == LIST;
}

 const struct yml_node *
yml_get_value(const struct yml_node *node, const char *_path)
{
    char *path = strdup(_path);

    if (node->type == ROOT)
        node = node->root.root;

    for (const char *part = strtok(path, "."), *next_part = strtok(NULL, ".");
         part != NULL;
         part = next_part, next_part = strtok(NULL, "."))
    {
        assert(yml_is_dict(node));

        for (const struct llist_element *key = node->dict.keys.head,
                 *value = node->dict.values.head;
             key != NULL;
             key = key->next, value = value->next)
        {
            assert(yml_is_scalar(key->node));

            if (strcmp(key->node->scalar.value, part) == 0) {
                if (next_part == NULL) {
                    free(path);
                    return value->node;
                }

                node = value->node;
                break;
            }
        }
    }

    free(path);
    return NULL;
}

struct yml_list_iter
yml_list_iter(const struct yml_node *list)
{
    assert(yml_is_list(list));

    const struct llist_element *element = list->list.values.head;
    return (struct yml_list_iter){
        .node = element != NULL ? element->node : NULL,
        .private = element};
}

void
yml_list_next(struct yml_list_iter *iter)
{
    const struct llist_element *element = iter->private;
    if (element == NULL)
        return;

    const struct llist_element *next = element->next;
    iter->node = next != NULL ? next->node : NULL;
    iter->private = next;
}

size_t
yml_list_length(const struct yml_node *list)
{
    assert(yml_is_list(list));

    size_t length = 0;
    for (struct yml_list_iter it = yml_list_iter(list);
         it.node != NULL;
         yml_list_next(&it), length++)
        ;

    return length;
}

struct yml_dict_iter
yml_dict_iter(const struct yml_node *dict)
{
    assert(yml_is_dict(dict));

    const struct llist_element *key = dict->dict.keys.head;
    const struct llist_element *value = dict->dict.values.head;

    assert((key == NULL && value == NULL) ||
           (key != NULL && value != NULL));

    return (struct yml_dict_iter){
        .key = key != NULL ? key->node : NULL,
        .value = value != NULL ? value->node : NULL,
        .private1 = key,
        .private2 = value,
    };
}

void
yml_dict_next(struct yml_dict_iter *iter)
{
    const struct llist_element *key = iter->private1;
    const struct llist_element *value = iter->private2;
    if (key == NULL)
        return;

    const struct llist_element *next_key = key->next;
    const struct llist_element *next_value = value->next;

    iter->key = next_key != NULL ? next_key->node : NULL;
    iter->value = next_value != NULL ? next_value->node : NULL;
    iter->private1 = next_key;
    iter->private2 = next_value;
}

size_t
yml_dict_length(const struct yml_node *dict)
{
    assert(yml_is_dict(dict));
    assert(dict->dict.key_count == dict->dict.value_count);
    return dict->dict.key_count;
}

const char *
yml_value_as_string(const struct yml_node *value)
{
    assert(yml_is_scalar(value));
    return value->scalar.value;
}

long
yml_value_as_int(const struct yml_node *value)
{
    assert(yml_is_scalar(value));

    long ival;
    int res = sscanf(yml_value_as_string(value), "%ld", &ival);
    return res != 1 ? -1 : ival;
}

bool
yml_value_as_bool(const struct yml_node *value)
{
    const char *v = yml_value_as_string(value);
    if (strcasecmp(v, "y") == 0 ||
        strcasecmp(v, "yes") == 0 ||
        strcasecmp(v, "true") == 0 ||
        strcasecmp(v, "on") == 0)
    {
        return true;
    } else if (strcasecmp(v, "n") == 0 ||
               strcasecmp(v, "no") == 0 ||
               strcasecmp(v, "false") == 0 ||
               strcasecmp(v, "off") == 0)
    {
        return false;
    } else
        assert(false);
}

static void
_print_node(const struct yml_node *n, int indent)
{
    switch (n->type) {
    case ROOT:
        _print_node(n->root.root, indent);
        break;

    case DICT:
        assert(n->dict.key_count == n->dict.value_count);
        for (const struct llist_element *k = n->dict.keys.head, *v = n->dict.values.head;
             k != NULL; k = k->next, v = v->next)
        {
            _print_node(k->node, indent);
            printf(": ");

            if (v->node->type != SCALAR) {
                printf("\n");
                _print_node(v->node, indent + 2);
            } else {
                _print_node(v->node, 0);
                printf("\n");
            }
        }
        break;

    case LIST:
        for (const struct llist_element *v = n->list.values.head;
             v != NULL;
             v = v->next)
        {
            printf("%*s- ", indent, "");
            if (v->node->type != SCALAR) {
                printf("\n");
                _print_node(v->node, indent + 2);
            } else {
                _print_node(v->node, 0);
            }
        }
        break;

    case SCALAR:
        printf("%*s%s", indent, "", n->scalar.value);
        break;
    }
}

void
print_node(const struct yml_node *n)
{
    _print_node(n, 0);
}
