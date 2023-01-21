#include "spa/utils/list.h"
#include <json-c/json.h>
#include <pipewire/impl-metadata.h>
#include <pipewire/pipewire.h>
#include <poll.h>
#include <spa/debug/pod.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/utils/json.h>
#include <spa/utils/result.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_MODULE "pipewire"
#define LOG_ENABLE_DBG 0
#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../module.h"
#include "../particle.h"
#include "../particles/dynlist.h"
#include "../plugin.h"
#include "../yml.h"

#define ARRAY_LENGTH(x) (sizeof((x)) / sizeof((x)[0]))
/* clang-format off */
#define X_FREE_SET(t, v) do { free((t)); (t) = (v); } while (0)
/* clang-format on */
#define X_STRDUP(s) ((s) != NULL ? strdup((s)) : NULL)

struct output_informations {
    /* internal */
    uint32_t device_id;
    uint32_t card_profile_device_id;

    /* informations */
    bool muted;
    uint16_t linear_volume; /* classic volume */
    uint16_t cubic_volume;  /* volume a la pulseaudio */
    char *name;
    char *description;
    char *form_factor; /* headset, headphone, speaker, ..., can be null */
    char *bus;         /* alsa, bluetooth, etc */
    char *icon;
};
static struct output_informations const output_informations_null;

struct data;
struct private
{
    struct particle *label;
    struct data *data;

    /* pipewire related */
    struct output_informations sink_informations;
    struct output_informations source_informations;
};

/* This struct is needed because when param event occur, the function
 * `node_events_param` will receive the corresponding event about the node
 * but there's no simple way of knowing from which node the event come from */
struct node_data {
    struct data *data;
    /* otherwise is_source */
    bool is_sink;
};

/* struct data */
struct node;
struct data {
    /* yambar module */
    struct module *module;

    char *target_sink;
    char *target_source;

    struct node *binded_sink;
    struct node *binded_source;

    struct node_data node_data_sink;
    struct node_data node_data_source;

    /* proxies */
    void *metadata;
    void *node_sink;
    void *node_source;

    /* main struct */
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;

    /* listener */
    struct spa_hook registry_listener;
    struct spa_hook core_listener;
    struct spa_hook metadata_listener;
    struct spa_hook node_sink_listener;
    struct spa_hook node_source_listener;

    /* list */
    struct spa_list node_list;
    struct spa_list device_list;

    int sync;
};

/* struct Route */
struct route {
    struct device *device;

    struct spa_list link;

    enum spa_direction direction; /* direction */
    int profile_device_id;        /* device */
    char *form_factor;            /* info.type */
    char *icon_name;              /* info.icon-name */
};

static void
route_free(struct route *route)
{
    free(route->form_factor);
    free(route->icon_name);
    spa_list_remove(&route->link);
    free(route);
}

/* struct Device */
struct device {
    struct data *data;

    struct spa_list link;
    uint32_t id;
    struct spa_list routes;

    void *proxy;
    struct spa_hook listener;
};

static void
device_free(struct device *device, struct data *data)
{
    struct route *route = NULL;
    spa_list_consume(route, &device->routes, link) route_free(route);

    spa_hook_remove(&device->listener);
    pw_proxy_destroy((struct pw_proxy *)device->proxy);

    spa_list_remove(&device->link);
    free(device);
}

static struct route *
route_find_or_create(struct device *device, uint32_t profile_device_id)
{
    struct route *route = NULL;
    spa_list_for_each(route, &device->routes, link)
    {
        if (route->profile_device_id == profile_device_id)
            return route;
    }

    /* route not found, let's create it */
    route = calloc(1, sizeof(struct route));
    assert(route != NULL);
    route->device = device;
    route->profile_device_id = profile_device_id;
    spa_list_append(&device->routes, &route->link);
    return route;
}

struct node {
    struct spa_list link;
    uint32_t id;
    char *name;
};

/* struct node */
static struct route *
node_find_route(struct data *data, bool is_sink)
{
    struct private *private = data->module->private;
    struct output_informations *output_informations = NULL;

    if (is_sink) {
        if (data->node_sink == NULL)
            return NULL;
        output_informations = &private->sink_informations;
    } else {
        if (data->node_source == NULL)
            return NULL;
        output_informations = &private->source_informations;
    }

    struct device *device = NULL;
    spa_list_for_each(device, &data->device_list, link)
    {
        if (device->id != output_informations->device_id)
            continue;

        struct route *route = NULL;
        spa_list_for_each(route, &device->routes, link)
        {
            if (route->profile_device_id == output_informations->card_profile_device_id)
                return route;
        }
    }

    return NULL;
}

static void
node_unhook_binded_node(struct data *data, bool is_sink)
{
    struct node **target_node = NULL;
    struct spa_hook *target_listener = NULL;
    void **target_proxy = NULL;

    if (is_sink) {
        target_node = &data->binded_sink;
        target_listener = &data->node_sink_listener;
        target_proxy = &data->node_sink;
    } else {
        target_node = &data->binded_source;
        target_listener = &data->node_source_listener;
        target_proxy = &data->node_source;
    }

    if (*target_node == NULL)
        return;

    spa_hook_remove(target_listener);
    pw_proxy_destroy(*target_proxy);

    *target_node = NULL;
    *target_proxy = NULL;
}

static void
node_free(struct node *node, struct data *data)
{
    if (data->binded_sink == node)
        node_unhook_binded_node(data, true);
    else if (data->binded_source == node)
        node_unhook_binded_node(data, false);

    spa_list_remove(&node->link);
    free(node->name);
    free(node);
}

/* Device events */
static void
device_events_info(void *userdata, const struct pw_device_info *info)
{
    struct device *device = userdata;

    /* We only want the "Route" param, which is in Params */
    if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS))
        return;

    for (size_t i = 0; i < info->n_params; ++i) {
        if (info->params[i].id == SPA_PARAM_Route) {
            pw_device_enum_params(device->proxy, 0, info->params[i].id, 0, -1, NULL);
            break;
        }
    }
}

static void
device_events_param(void *userdata, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
    /* We should only receive ParamRoute */
    assert(spa_pod_is_object_type(param, SPA_TYPE_OBJECT_ParamRoute));

    struct route data = {0};
    struct spa_pod_prop const *prop = NULL;

    /* device must be present otherwise I can't do anything with the data */
    prop = spa_pod_find_prop(param, NULL, SPA_PARAM_ROUTE_device);
    if (prop == NULL)
        return;
    spa_pod_get_int(&prop->value, &data.profile_device_id);

    /* same for direction, required too */
    prop = spa_pod_find_prop(param, NULL, SPA_PARAM_ROUTE_direction);
    if (prop == NULL)
        return;
    char const *direction = NULL;
    spa_pod_get_string(&prop->value, &direction);
    if (spa_streq(direction, "Output"))
        data.direction = SPA_DIRECTION_OUTPUT;
    else
        data.direction = SPA_DIRECTION_INPUT;

    /* same for info, it's required */
    prop = spa_pod_find_prop(param, NULL, SPA_PARAM_ROUTE_info);
    if (prop == NULL)
        return;

    struct spa_pod *iter = NULL;
    char const *header = NULL;
    SPA_POD_STRUCT_FOREACH(&prop->value, iter)
    {
        /* no previous header */
        if (header == NULL) {
            /* headers are always string */
            if (spa_pod_is_string(iter))
                spa_pod_get_string(iter, &header);
            /* otherwise it's the first iteration (number of elements in the struct) */
            continue;
        }

        /* Values needed:
         * - (string) device.icon_name [icon_name]
         * - (string) port.type [form_factor] */
        if (spa_pod_is_string(iter)) {
            if (spa_streq(header, "device.icon_name"))
                spa_pod_get_string(iter, (char const **)&data.icon_name);
            else if (spa_streq(header, "port.type")) {
                spa_pod_get_string(iter, (char const **)&data.form_factor);
            }
        }

        header = NULL;
    }

    struct device *device = userdata;

    struct route *route = route_find_or_create(device, data.profile_device_id);
    X_FREE_SET(route->form_factor, X_STRDUP(data.form_factor));
    X_FREE_SET(route->icon_name, X_STRDUP(data.icon_name));
    route->direction = data.direction;

    /* set missing informations if possible */
    struct private *private = device->data->module->private;
    struct node *binded_node = NULL;
    struct output_informations *output_informations = NULL;

    if (route->direction == SPA_DIRECTION_INPUT) {
        binded_node = private->data->binded_source;
        output_informations = &private->source_informations;
    } else {
        binded_node = private->data->binded_sink;
        output_informations = &private->sink_informations;
    }

    /* Node not binded */
    if (binded_node == NULL)
        return;

    /* Node's device is the the same as route's device */
    if (output_informations->device_id != route->device->id)
        return;

    /* Route is not the Node's device route */
    if (output_informations->card_profile_device_id != route->profile_device_id)
        return;

    /* Update missing informations */
    X_FREE_SET(output_informations->form_factor, X_STRDUP(route->form_factor));
    X_FREE_SET(output_informations->icon, X_STRDUP(route->icon_name));

    device->data->module->bar->refresh(device->data->module->bar);
}

static struct pw_device_events const device_events = {
    PW_VERSION_DEVICE_EVENTS,
    .info = device_events_info,
    .param = device_events_param,
};

/* Node events */
static void
node_events_info(void *userdata, struct pw_node_info const *info)
{
    struct node_data *node_data = userdata;
    struct data *data = node_data->data;
    struct private *private = data->module->private;

    if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
        /* We only need the Props param, so let's try to find it */
        for (size_t i = 0; i < info->n_params; ++i) {
            if (info->params[i].id == SPA_PARAM_Props) {
                void *target_node = (node_data->is_sink ? data->node_sink : data->node_source);
                /* Found it, will emit a param event, the parem will then be handled
                 * in node_events_param */
                pw_node_enum_params(target_node, 0, info->params[i].id, 0, -1, NULL);
                break;
            }
        }
    }

    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
        struct output_informations *output_informations
            = (node_data->is_sink ? &private->sink_informations : &private->source_informations);
        struct spa_dict_item const *item = NULL;

        item = spa_dict_lookup_item(info->props, "node.name");
        if (item != NULL)
            X_FREE_SET(output_informations->name, X_STRDUP(item->value));

        item = spa_dict_lookup_item(info->props, "node.description");
        if (item != NULL)
            X_FREE_SET(output_informations->description, X_STRDUP(item->value));

        item = spa_dict_lookup_item(info->props, "device.id");
        if (item != NULL) {
            uint32_t value = 0;
            spa_atou32(item->value, &value, 10);
            output_informations->device_id = value;
        }

        item = spa_dict_lookup_item(info->props, "card.profile.device");
        if (item != NULL) {
            uint32_t value = 0;
            spa_atou32(item->value, &value, 10);
            output_informations->card_profile_device_id = value;
        }

        /* Device's informations has an more important priority than node's informations */
        /* icon_name */
        struct route *route = node_find_route(data, node_data->is_sink);
        if (route != NULL && route->icon_name != NULL)
            output_informations->icon = X_STRDUP(route->icon_name);
        else {
            item = spa_dict_lookup_item(info->props, "device.icon-name");
            if (item != NULL)
                X_FREE_SET(output_informations->icon, X_STRDUP(item->value));
        }
        /* form_factor */
        if (route != NULL && route->form_factor != NULL)
            output_informations->form_factor = X_STRDUP(route->form_factor);
        else {
            item = spa_dict_lookup_item(info->props, "device.form-factor");
            if (item != NULL)
                X_FREE_SET(output_informations->form_factor, X_STRDUP(item->value));
        }

        item = spa_dict_lookup_item(info->props, "device.bus");
        if (item != NULL)
            X_FREE_SET(output_informations->bus, X_STRDUP(item->value));

        data->module->bar->refresh(data->module->bar);
    }
}

static void
node_events_param(void *userdata, __attribute__((unused)) int seq, __attribute__((unused)) uint32_t id,
                  __attribute__((unused)) uint32_t index, __attribute__((unused)) uint32_t next,
                  const struct spa_pod *param)
{
    struct node_data *node_data = userdata;
    struct data *data = node_data->data;
    struct private *private = data->module->private;

    struct output_informations *output_informations
        = (node_data->is_sink ? &private->sink_informations : &private->source_informations);
    struct spa_pod_prop const *prop = NULL;

    prop = spa_pod_find_prop(param, NULL, SPA_PROP_mute);
    if (prop != NULL) {
        bool value = false;
        spa_pod_get_bool(&prop->value, &value);
        output_informations->muted = value;
    }

    prop = spa_pod_find_prop(param, NULL, SPA_PROP_channelVolumes);
    if (prop != NULL) {
        uint32_t n_values = 0;
        float *values = spa_pod_get_array(&prop->value, &n_values);
        float total = 0.0f;

        /* Array can be empty some times, assume that values have not changed */
        if (n_values != 0) {
            for (uint32_t i = 0; i < n_values; ++i)
                total += values[i];

            float base_volume = total / n_values;
            output_informations->linear_volume = roundf(base_volume * 100);
            output_informations->cubic_volume = roundf(cbrtf(base_volume) * 100);
        }
    }

    data->module->bar->refresh(data->module->bar);
}

static struct pw_node_events const node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = node_events_info,
    .param = node_events_param,
};

/* Metadata events */
static int
metadata_property(void *userdata, __attribute__((unused)) uint32_t id, char const *key,
                  __attribute__((unused)) char const *type, char const *value)
{
    struct data *data = userdata;
    bool is_sink = false; // true for source mode
    char **target_name = NULL;

    /* We only want default.audio.sink or default.audio.source */
    if (spa_streq(key, "default.audio.sink")) {
        is_sink = true;
        target_name = &data->target_sink;
    } else if (spa_streq(key, "default.audio.source")) {
        is_sink = false; /* just to be explicit */
        target_name = &data->target_source;
    } else
        return 0;

    /* Value is NULL when the profile is set to `off`. */
    if (value == NULL) {
        node_unhook_binded_node(data, is_sink);
        free(*target_name);
        *target_name = NULL;
        data->module->bar->refresh(data->module->bar);
        return 0;
    }

    struct json_object *json = json_tokener_parse(value);
    struct json_object_iterator json_it = json_object_iter_begin(json);
    struct json_object_iterator json_it_end = json_object_iter_end(json);

    while (!json_object_iter_equal(&json_it, &json_it_end)) {
        char const *key = json_object_iter_peek_name(&json_it);
        if (!spa_streq(key, "name")) {
            json_object_iter_next(&json_it);
            continue;
        }

        /* Found name */
        struct json_object *value = json_object_iter_peek_value(&json_it);
        assert(json_object_is_type(value, json_type_string));

        char const *name = json_object_get_string(value);
        /* `auto_null` is the same as `value == NULL` see lines above. */
        if (spa_streq(name, "auto_null")) {
            node_unhook_binded_node(data, is_sink);
            free(*target_name);
            *target_name = NULL;
            data->module->bar->refresh(data->module->bar);
            break;
        }

        /* target_name is the same */
        if (spa_streq(name, *target_name))
            break;

        /* Unhook the binded_node */
        node_unhook_binded_node(data, is_sink);

        /* Update the target */
        free(*target_name);
        *target_name = strdup(name);

        /* Sync the core, core_events_done will then try to bind the good node */
        data->sync = pw_core_sync(data->core, PW_ID_CORE, data->sync);
        break;
    }

    json_object_put(json);

    return 0;
}

static struct pw_metadata_events const metadata_events = {
    PW_VERSION_METADATA_EVENTS,
    .property = metadata_property,
};

/* Registry events */
static void
registry_event_global(void *userdata, uint32_t id, __attribute__((unused)) uint32_t permissions, char const *type,
                      __attribute__((unused)) uint32_t version, struct spa_dict const *props)
{
    struct data *data = userdata;

    /* New device */
    if (spa_streq(type, PW_TYPE_INTERFACE_Device)) {
        struct device *device = calloc(1, sizeof(struct device));
        assert(device != NULL);
        device->data = data;
        device->id = id;
        spa_list_init(&device->routes);
        device->proxy = pw_registry_bind(data->registry, id, type, PW_VERSION_DEVICE, 0);
        assert(device->proxy != NULL);
        pw_device_add_listener(device->proxy, &device->listener, &device_events, device);

        spa_list_append(&data->device_list, &device->link);
    }
    /* New node */
    else if (spa_streq(type, PW_TYPE_INTERFACE_Node)) {
        /* Fill a new node */
        struct node *node = calloc(1, sizeof(struct node));
        assert(node != NULL);
        node->id = id;
        node->name = strdup(spa_dict_lookup(props, PW_KEY_NODE_NAME));

        /* Store it */
        spa_list_append(&data->node_list, &node->link);
    }
    /* New metadata */
    else if (spa_streq(type, PW_TYPE_INTERFACE_Metadata)) {
        /* A metadata has already been bind */
        if (data->metadata != NULL)
            return;

        /* Target only metadata which has "default" key */
        char const *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (name == NULL || !spa_streq(name, "default"))
            return;

        /* Bind metadata */
        data->metadata = pw_registry_bind(data->registry, id, type, PW_VERSION_METADATA, 0);
        assert(data->metadata != NULL);
        pw_metadata_add_listener(data->metadata, &data->metadata_listener, &metadata_events, data);
    }

    /* `core_events_done` will then try to bind the good node */
    data->sync = pw_core_sync(data->core, PW_ID_CORE, data->sync);
}

static void
registry_event_global_remove(void *userdata, uint32_t id)
{
    struct data *data = userdata;

    /* Try to find a node with the same `id` */
    struct node *node = NULL, *node_temp = NULL;
    spa_list_for_each_safe(node, node_temp, &data->node_list, link)
    {
        if (node->id == id) {
            node_free(node, data);
            return;
        }
    }

    /* No node with this `id` maybe it's a device */
    struct device *device = NULL, *device_temp = NULL;
    spa_list_for_each_safe(device, device_temp, &data->device_list, link)
    {
        if (device->id == id) {
            device_free(device, data);
            return;
        }
    }
}

static struct pw_registry_events const registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static void
try_to_bind_node(struct node_data *node_data, char const *target_name, struct node **target_node, void **target_proxy,
                 struct spa_hook *target_listener)
{
    /* profile deactived */
    if (target_name == NULL)
        return;

    struct data *data = node_data->data;

    struct node *node = NULL;
    spa_list_for_each(node, &data->node_list, link)
    {
        if (!spa_streq(target_name, node->name))
            continue;

        /* Found good node */

        *target_node = node;
        *target_proxy = pw_registry_bind(data->registry, node->id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
        assert(*target_proxy != NULL);
        pw_node_add_listener(*target_proxy, target_listener, &node_events, node_data);
        break;
    }
}

/* Core events */
static void
core_events_done(void *userdata, uint32_t id, int seq)
{
    struct data *data = userdata;

    if (id != PW_ID_CORE)
        return;

    /* Not our seq */
    if (data->sync != seq)
        return;

    /* Sync ended, try to bind the node which has the targeted sink or the targeted source */

    /* Node sink not binded and target_sink is set */
    if (data->binded_sink == NULL && data->target_sink != NULL)
        try_to_bind_node(&data->node_data_sink, data->target_sink, &data->binded_sink, &data->node_sink,
                         &data->node_sink_listener);

    /* Node source not binded and target_source is set */
    if (data->binded_source == NULL && data->target_source != NULL)
        try_to_bind_node(&data->node_data_source, data->target_source, &data->binded_source, &data->node_source,
                         &data->node_source_listener);
}

static void
core_events_error(void *userdata, uint32_t id, int seq, int res, char const *message)
{
    pw_log_error("error id:%u seq:%d res:%d (%s): %s", id, seq, res, spa_strerror(res), message);

    if (id == PW_ID_CORE && res == -EPIPE) {
        struct data *data = userdata;
        pw_main_loop_quit(data->loop);
    }
}

static struct pw_core_events const core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = core_events_done,
    .error = core_events_error,
};

/* init, deinit */
static struct data *
pipewire_init(struct module *module)
{
    pw_init(NULL, NULL);

    /* Data */
    struct data *data = calloc(1, sizeof(struct data));
    assert(data != NULL);

    spa_list_init(&data->node_list);
    spa_list_init(&data->device_list);

    /* Main loop */
    data->loop = pw_main_loop_new(NULL);
    if (data->loop == NULL) {
        LOG_ERR("failed to instantiate main loop");
        goto err;
    }

    /* Context */
    data->context = pw_context_new(pw_main_loop_get_loop(data->loop), NULL, 0);
    if (data->context == NULL) {
        LOG_ERR("failed to instantiate pipewire context");
        goto err;
    }

    /* Core */
    data->core = pw_context_connect(data->context, NULL, 0);
    if (data->core == NULL) {
        LOG_ERR("failed to connect to pipewire");
        goto err;
    }
    pw_core_add_listener(data->core, &data->core_listener, &core_events, data);

    /* Registry */
    data->registry = pw_core_get_registry(data->core, PW_VERSION_REGISTRY, 0);
    if (data->registry == NULL) {
        LOG_ERR("failed to get core registry");
        goto err;
    }
    pw_registry_add_listener(data->registry, &data->registry_listener, &registry_events, data);

    /* Sync */
    data->sync = pw_core_sync(data->core, PW_ID_CORE, data->sync);

    data->module = module;

    /* node_events_param_data */
    data->node_data_sink.data = data;
    data->node_data_sink.is_sink = true;
    data->node_data_source.data = data;
    data->node_data_source.is_sink = false;

    return data;

err:
    if (data->registry != NULL)
        pw_proxy_destroy((struct pw_proxy *)data->registry);
    if (data->core != NULL)
        pw_core_disconnect(data->core);
    if (data->context != NULL)
        pw_context_destroy(data->context);
    if (data->loop != NULL)
        pw_main_loop_destroy(data->loop);
    free(data);
    return NULL;
}

static void
pipewire_deinit(struct data *data)
{
    if (data == NULL)
        return;

    struct node *node = NULL;
    spa_list_consume(node, &data->node_list, link) node_free(node, data);

    struct device *device = NULL;
    spa_list_consume(device, &data->device_list, link) device_free(device, data);

    if (data->metadata)
        pw_proxy_destroy((struct pw_proxy *)data->metadata);
    spa_hook_remove(&data->registry_listener);
    pw_proxy_destroy((struct pw_proxy *)data->registry);
    spa_hook_remove(&data->core_listener);
    spa_hook_remove(&data->metadata_listener);
    pw_core_disconnect(data->core);
    pw_context_destroy(data->context);
    pw_main_loop_destroy(data->loop);
    free(data->target_sink);
    free(data->target_source);
    pw_deinit();
}

static void
destroy(struct module *module)
{
    struct private *private = module->private;

    pipewire_deinit(private->data);
    private->label->destroy(private->label);

    /* sink */
    free(private->sink_informations.name);
    free(private->sink_informations.description);
    free(private->sink_informations.icon);
    free(private->sink_informations.form_factor);
    free(private->sink_informations.bus);
    /* source */
    free(private->source_informations.name);
    free(private->source_informations.description);
    free(private->source_informations.icon);
    free(private->source_informations.form_factor);
    free(private->source_informations.bus);

    free(private);
    module_default_destroy(module);
}

static char const *
description(const struct module *module)
{
    return "pipewire";
}

static struct exposable *
content(struct module *module)
{
    struct private *private = module->private;

    if (private->data == NULL)
        return dynlist_exposable_new(NULL, 0, 0, 0);

    mtx_lock(&module->lock);

    struct exposable *exposables[2];
    size_t exposables_length = ARRAY_LENGTH(exposables);

    struct output_informations const *output_informations = NULL;

    /* sink */
    output_informations
        = (private->data->target_sink == NULL
           ? &output_informations_null
           : &private->sink_informations);

    struct tag_set sink_tag_set = (struct tag_set){
        .tags = (struct tag *[]){
            tag_new_string(module, "type", "sink"),
            tag_new_string(module, "name", output_informations->name),
            tag_new_string(module, "description", output_informations->description),
            tag_new_string(module, "icon", output_informations->icon),
            tag_new_string(module, "form_factor", output_informations->form_factor),
            tag_new_string(module, "bus", output_informations->bus),
            tag_new_bool(module, "muted", output_informations->muted),
            tag_new_int_range(module, "linear_volume", output_informations->linear_volume, 0, 100),
            tag_new_int_range(module, "cubic_volume", output_informations->cubic_volume, 0, 100),
        },
        .count = 9,
    };

    /* source */
    output_informations
        = (private->data->target_source == NULL
           ? &output_informations_null
           : &private->source_informations);

    struct tag_set source_tag_set = (struct tag_set){
        .tags = (struct tag *[]){
            tag_new_string(module, "type", "source"),
            tag_new_string(module, "name", output_informations->name),
            tag_new_string(module, "description", output_informations->description),
            tag_new_string(module, "icon", output_informations->icon),
            tag_new_string(module, "form_factor", output_informations->form_factor),
            tag_new_string(module, "bus", output_informations->bus),
            tag_new_bool(module, "muted", output_informations->muted),
            tag_new_int_range(module, "linear_volume", output_informations->linear_volume, 0, 100),
            tag_new_int_range(module, "cubic_volume", output_informations->cubic_volume, 0, 100),
        },
        .count = 9,
    };

    exposables[0] = private->label->instantiate(private->label, &sink_tag_set);
    exposables[1] = private->label->instantiate(private->label, &source_tag_set);

    tag_set_destroy(&sink_tag_set);
    tag_set_destroy(&source_tag_set);

    mtx_unlock(&module->lock);

    return dynlist_exposable_new(exposables, exposables_length, 0, 0);
}

static int
run(struct module *module)
{
    struct private *private = module->private;
    if (private->data == NULL)
        return 1;

    struct pw_loop *pw_loop = pw_main_loop_get_loop(private->data->loop);
    struct pollfd pollfds[] = {
        /* abort_fd */
        (struct pollfd){.fd = module->abort_fd, .events = POLLIN},
        /* pipewire */
        (struct pollfd){.fd = pw_loop_get_fd(pw_loop), .events = POLLIN},
    };

    while (true) {
        if (poll(pollfds, ARRAY_LENGTH(pollfds), -1) == -1) {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("Unable to poll: %s", strerror(errno));
            break;
        }

        /* abort_fd */
        if (pollfds[0].revents & POLLIN)
            break;

        /* pipewire */
        if (!(pollfds[1].revents & POLLIN))
            /* issue happened */
            break;

        int result = pw_loop_iterate(pw_loop, 0);
        if (result < 0) {
            LOG_ERRNO("Unable to iterate pipewire loop: %s", spa_strerror(result));
            break;
        }
    }

    return 0;
}

static struct module *
pipewire_new(struct particle *label)
{
    struct private *private = calloc(1, sizeof(struct private));
    assert(private != NULL);
    private->label = label;

    struct module *module = module_common_new();
    module->private = private;
    module->run = &run;
    module->destroy = &destroy;
    module->content = &content;
    module->description = &description;

    private->data = pipewire_init(module);

    return module;
}

static struct module *
from_conf(struct yml_node const *node, struct conf_inherit inherited)
{
    struct yml_node const *content = yml_get_value(node, "content");
    return pipewire_new(conf_to_particle(content, inherited));
}

static bool
verify_conf(keychain_t *keychain, struct yml_node const *node)
{
    static struct attr_info const attrs[] = {
        MODULE_COMMON_ATTRS,
    };
    return conf_verify_dict(keychain, node, attrs);
}

struct module_iface const module_pipewire_iface = {
    .from_conf = &from_conf,
    .verify_conf = &verify_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern struct module_iface const iface __attribute__((weak, alias("module_pipewire_iface")));
#endif
