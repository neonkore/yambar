#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/timerfd.h>

#include <pulse/pulseaudio.h>

#define LOG_MODULE "pulse"
#define LOG_ENABLE_DBG 0
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../plugin.h"

struct private {
    char *sink_name;
    char *source_name;
    struct particle *label;

    bool online;

    bool sink_online;
    pa_cvolume sink_volume;
    bool sink_muted;
    char *sink_port;
    uint32_t sink_index;

    bool source_online;
    pa_cvolume source_volume;
    bool source_muted;
    char *source_port;
    uint32_t source_index;

    int refresh_timer_fd;
    bool refresh_scheduled;

    pa_mainloop *mainloop;
    pa_context *context;
};

static void
destroy(struct module *mod)
{
    struct private *priv = mod->private;
    priv->label->destroy(priv->label);
    free(priv->sink_name);
    free(priv->source_name);
    free(priv->sink_port);
    free(priv->source_port);
    free(priv);
    module_default_destroy(mod);
}

static const char *
description(struct module *mod)
{
    return "pulse";
}

static struct exposable *
content(struct module *mod)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);

    pa_volume_t sink_volume_max   = pa_cvolume_max(&priv->sink_volume);
    pa_volume_t source_volume_max = pa_cvolume_max(&priv->source_volume);
    int sink_percent   = round(100.0 * sink_volume_max   / PA_VOLUME_NORM);
    int source_percent = round(100.0 * source_volume_max / PA_VOLUME_NORM);

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_bool(mod, "online", priv->online),

            tag_new_bool(mod, "sink_online", priv->sink_online),
            tag_new_int_range(mod, "sink_percent", sink_percent, 0, 100),
            tag_new_bool(mod, "sink_muted", priv->sink_muted),
            tag_new_string(mod, "sink_port", priv->sink_port),

            tag_new_bool(mod, "source_online", priv->source_online),
            tag_new_int_range(mod, "source_percent", source_percent, 0, 100),
            tag_new_bool(mod, "source_muted", priv->source_muted),
            tag_new_string(mod, "source_port", priv->source_port),
        },
        .count = 9,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable = priv->label->instantiate(priv->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static const char *
context_error(pa_context *c)
{
    return pa_strerror(pa_context_errno(c));
}

static void
abort_event_cb(pa_mainloop_api *api,
               pa_io_event *event,
               int fd,
               pa_io_event_flags_t flags,
               void *userdata)
{
    struct module *mod = userdata;
    struct private *priv = mod->private;

    pa_context_disconnect(priv->context);
}

static void
refresh_timer_cb(pa_mainloop_api *api,
                 pa_io_event *event,
                 int fd,
                 pa_io_event_flags_t flags,
                 void *userdata)
{
    struct module *mod = userdata;
    struct private *priv = mod->private;

    // Drain the refresh timer.
    uint64_t n;
    if (read(priv->refresh_timer_fd, &n, sizeof n) < 0)
        LOG_ERRNO("failed to read from timerfd");

    // Clear the refresh flag.
    priv->refresh_scheduled = false;

    // Refresh the bar.
    mod->bar->refresh(mod->bar);
}

// Refresh the bar after a small delay. Without the delay, the bar
// would be refreshed multiple times per event (e.g., a volume change),
// and sometimes the active port would be reported incorrectly for a
// brief moment. (This behavior was observed with PipeWire 0.3.61.)
static void
schedule_refresh(struct module *mod)
{
    struct private *priv = mod->private;

    // Do nothing if a refresh has already been scheduled.
    if (priv->refresh_scheduled)
        return;

    // Start the refresh timer.
    struct itimerspec t = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value    = { .tv_sec = 0, .tv_nsec = 50000000 },
    };
    timerfd_settime(priv->refresh_timer_fd, 0, &t, NULL);

    // Set the refresh flag.
    priv->refresh_scheduled = true;
}

static void
set_server_online(struct module *mod)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);
    priv->online = true;
    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
set_server_offline(struct module *mod)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);
    priv->online = false;
    priv->sink_online = false;
    priv->source_online = false;
    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
set_sink_info(struct module *mod, const pa_sink_info *sink_info)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);

    free(priv->sink_port);

    priv->sink_online = true;
    priv->sink_index  = sink_info->index;
    priv->sink_volume = sink_info->volume;
    priv->sink_muted  = sink_info->mute;
    priv->sink_port   = sink_info->active_port != NULL
                      ? strdup(sink_info->active_port->description)
                      : NULL;

    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
set_sink_offline(struct module *mod)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);
    priv->sink_online = false;
    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
set_source_info(struct module *mod, const pa_source_info *source_info)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);

    free(priv->source_port);

    priv->source_online = true;
    priv->source_index  = source_info->index;
    priv->source_volume = source_info->volume;
    priv->source_muted  = source_info->mute;
    priv->source_port   = source_info->active_port != NULL
                        ? strdup(source_info->active_port->description)
                        : NULL;

    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
set_source_offline(struct module *mod)
{
    struct private *priv = mod->private;

    mtx_lock(&mod->lock);
    priv->source_online = false;
    mtx_unlock(&mod->lock);

    schedule_refresh(mod);
}

static void
sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    struct module *mod = userdata;

    if (eol < 0) {
        LOG_ERR("failed to get sink info: %s", context_error(c));
        set_sink_offline(mod);
    } else if (eol == 0) {
        set_sink_info(mod, i);
    }
}

static void
source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    struct module *mod = userdata;

    if (eol < 0) {
        LOG_ERR("failed to get source info: %s", context_error(c));
        set_source_offline(mod);
    } else if (eol == 0) {
        set_source_info(mod, i);
    }
}

static void
server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
    LOG_INFO("%s, version %s", i->server_name, i->server_version);
}

static void
get_sink_info_by_name(pa_context *c, const char *name, void *userdata)
{
    pa_operation *o =
        pa_context_get_sink_info_by_name(c, name, sink_info_cb, userdata);
    pa_operation_unref(o);
}

static void
get_source_info_by_name(pa_context *c, const char *name, void *userdata)
{
    pa_operation *o =
        pa_context_get_source_info_by_name(c, name, source_info_cb, userdata);
    pa_operation_unref(o);
}

static void
get_sink_info_by_index(pa_context *c, uint32_t index, void *userdata)
{
    pa_operation *o =
        pa_context_get_sink_info_by_index(c, index, sink_info_cb, userdata);
    pa_operation_unref(o);
}

static void
get_source_info_by_index(pa_context *c, uint32_t index, void *userdata)
{
    pa_operation *o =
        pa_context_get_source_info_by_index(c, index, source_info_cb, userdata);
    pa_operation_unref(o);
}

static void
get_server_info(pa_context *c, void *userdata)
{
    pa_operation *o = pa_context_get_server_info(c, server_info_cb, userdata);
    pa_operation_unref(o);
}

static void
subscribe(pa_context *c, void *userdata)
{
    pa_subscription_mask_t mask = PA_SUBSCRIPTION_MASK_SERVER
                                | PA_SUBSCRIPTION_MASK_SINK
                                | PA_SUBSCRIPTION_MASK_SOURCE;
    pa_operation *o = pa_context_subscribe(c, mask, NULL, userdata);
    pa_operation_unref(o);
}

static pa_context *
connect_to_server(struct module *mod);

static void
context_state_change_cb(pa_context *c, void *userdata)
{
    struct module *mod = userdata;
    struct private *priv = mod->private;

    pa_context_state_t state = pa_context_get_state(c);
    switch (state) {
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        break;

    case PA_CONTEXT_READY:
        set_server_online(mod);
        subscribe(c, mod);
        get_server_info(c, mod);
        get_sink_info_by_name(c, priv->sink_name, mod);
        get_source_info_by_name(c, priv->source_name, mod);
        break;

    case PA_CONTEXT_FAILED:
        LOG_WARN("connection lost");
        set_server_offline(mod);
        pa_context_unref(priv->context);
        priv->context = connect_to_server(mod);
        break;

    case PA_CONTEXT_TERMINATED:
        LOG_DBG("connection terminated");
        set_server_offline(mod);
        pa_mainloop_quit(priv->mainloop, 0);
        break;
    }
}

static void
subscription_event_cb(pa_context *c,
                      pa_subscription_event_type_t event_type,
                      uint32_t index,
                      void *userdata)
{
    struct module *mod = userdata;
    struct private *priv = mod->private;

    int facility = event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    int type     = event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    switch (facility) {
    case PA_SUBSCRIPTION_EVENT_SERVER:
        get_sink_info_by_name(c, priv->sink_name, mod);
        get_source_info_by_name(c, priv->source_name, mod);
        break;

    case PA_SUBSCRIPTION_EVENT_SINK:
        if (index == priv->sink_index) {
            if (type == PA_SUBSCRIPTION_EVENT_CHANGE)
                get_sink_info_by_index(c, index, mod);
            else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                set_sink_offline(mod);
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SOURCE:
        if (index == priv->source_index) {
            if (type == PA_SUBSCRIPTION_EVENT_CHANGE)
                get_source_info_by_index(c, index, mod);
            else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                set_source_offline(mod);
        }
        break;
    }
}

static pa_context *
connect_to_server(struct module *mod)
{
    struct private *priv = mod->private;

    // Create connection context.
    pa_mainloop_api *api = pa_mainloop_get_api(priv->mainloop);
    pa_context *c = pa_context_new(api, "yambar");
    if (c == NULL) {
        LOG_ERR("failed to create PulseAudio connection context");
        return NULL;
    }

    // Register callback functions.
    pa_context_set_state_callback(c, context_state_change_cb, mod);
    pa_context_set_subscribe_callback(c, subscription_event_cb, mod);

    // Connect to server.
    pa_context_flags_t flags = PA_CONTEXT_NOFAIL
                             | PA_CONTEXT_NOAUTOSPAWN;
    if (pa_context_connect(c, NULL, flags, NULL) < 0) {
        LOG_ERR("failed to connect to PulseAudio server: %s", context_error(c));
        pa_context_unref(c);
        return NULL;
    }

    return c;
}

static int
run(struct module *mod)
{
    struct private *priv = mod->private;
    int ret = -1;

    // Create main loop.
    priv->mainloop = pa_mainloop_new();
    if (priv->mainloop == NULL) {
        LOG_ERR("failed to create PulseAudio main loop");
        return -1;
    }

    // Create refresh timer.
    priv->refresh_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (priv->refresh_timer_fd < 0) {
        LOG_ERRNO("failed to create timerfd");
        pa_mainloop_free(priv->mainloop);
        return -1;
    }

    // Connect to server.
    priv->context = connect_to_server(mod);
    if (priv->context == NULL) {
        pa_mainloop_free(priv->mainloop);
        close(priv->refresh_timer_fd);
        return -1;
    }

    // Poll refresh timer and abort event.
    pa_mainloop_api *api = pa_mainloop_get_api(priv->mainloop);
    api->io_new(api, priv->refresh_timer_fd, PA_IO_EVENT_INPUT,
                refresh_timer_cb, mod);
    api->io_new(api, mod->abort_fd, PA_IO_EVENT_INPUT | PA_IO_EVENT_HANGUP,
                abort_event_cb, mod);

    // Run main loop.
    if (pa_mainloop_run(priv->mainloop, &ret) < 0) {
        LOG_ERR("PulseAudio main loop error");
        ret = -1;
    }

    // Clean up.
    pa_context_unref(priv->context);
    pa_mainloop_free(priv->mainloop);
    close(priv->refresh_timer_fd);

    return ret;
}

static struct module *
pulse_new(const char *sink_name,
          const char *source_name,
          struct particle *label)
{
    struct private *priv = calloc(1, sizeof *priv);
    priv->label = label;
    priv->sink_name = strdup(sink_name);
    priv->source_name = strdup(source_name);

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *sink = yml_get_value(node, "sink");
    const struct yml_node *source = yml_get_value(node, "source");
    const struct yml_node *content = yml_get_value(node, "content");

    return pulse_new(
        sink != NULL ? yml_value_as_string(sink) : "@DEFAULT_SINK@",
        source != NULL ? yml_value_as_string(source) : "@DEFAULT_SOURCE@",
        conf_to_particle(content, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"sink", false, &conf_verify_string},
        {"source", false, &conf_verify_string},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_pulse_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_pulse_iface")));
#endif
