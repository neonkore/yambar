#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <alsa/asoundlib.h>

#include <tllist.h>

#define LOG_MODULE "alsa"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../plugin.h"

struct private {
    char *card;
    char *mixer;
    char *volume_channel;
    char *muted_channel;
    struct particle *label;

    tll(snd_mixer_selem_channel_id_t) channels;

    bool online;
    long vol_min;
    long vol_max;
    long vol_cur;
    bool muted;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    tll_free(m->channels);
    m->label->destroy(m->label);
    free(m->card);
    free(m->mixer);
    free(m->volume_channel);
    free(m->muted_channel);
    free(m);
    module_default_destroy(mod);
}

static const char *
description(struct module *mod)
{
    static char desc[32];
    struct private *m = mod->private;
    snprintf(desc, sizeof(desc), "alsa(%s)", m->card);
    return desc;
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    int percent = m->vol_max - m->vol_min > 0
        ? round(100. * m->vol_cur / (m->vol_max - m->vol_min))
        : 0;

    mtx_lock(&mod->lock);
    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_bool(mod, "online", m->online),
            tag_new_int_range(mod, "volume", m->vol_cur, m->vol_min, m->vol_max),
            tag_new_int_range(mod, "percent", percent, 0, 100),
            tag_new_bool(mod, "muted", m->muted),
        },
        .count = 4,
    };
    mtx_unlock(&mod->lock);

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static void
update_state(struct module *mod, snd_mixer_elem_t *elem)
{
    struct private *m = mod->private;

    /* Get min/max volume levels */
    long min = 0, max = 0;
    int r = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

    if (r < 0) {
        LOG_DBG("%s,%s: failed to get volume min/max (mixer is digital?)",
                m->card, m->mixer);
    }

    /* Make sure min <= max */
    if (min > max) {
        LOG_WARN(
            "%s,%s: indicated minimum volume is greater than the maximum: "
            "%ld > %ld", m->card, m->mixer, min, max);
        min = max;
    }

    long cur = 0;

    /* If volume level can be changed (i.e. this isn't just a switch;
     * e.g. a digital channel), get current level */
    if (max > 0) {
        tll_foreach(m->channels, it) {
            const char *name = snd_mixer_selem_channel_name(it->item);
            if (m->volume_channel != NULL && strcmp(name, m->volume_channel) != 0)
                continue;

            int r = snd_mixer_selem_get_playback_volume(elem, it->item, &cur);

            if (r < 0) {
                LOG_WARN("%s,%s: %s: failed to get current volume",
                         m->card, m->mixer, name);
            }

            LOG_DBG("%s,%s: %s: volume: %ld", m->card, m->mixer, name, cur);
        }
    }

    int unmuted = 0;

    /* Get muted state */
    tll_foreach(m->channels, it) {
        const char *name = snd_mixer_selem_channel_name(it->item);
        if (m->muted_channel != NULL && strcmp(name, m->muted_channel) != 0)
            continue;

        int r = snd_mixer_selem_get_playback_switch(elem, it->item, &unmuted);

        if (r < 0) {
            LOG_WARN("%s,%s: %s: failed to get muted state",
                     m->card, m->mixer, name);
            unmuted = 1;
        }

        LOG_DBG("%s,%s: %s: muted: %d", m->card, m->mixer, name, !unmuted);
    }

    /* Make sure min <= cur <= max */
    if (cur < min) {
        LOG_WARN(
            "%s,%s: current volume is less than the indicated minimum: "
            "%ld < %ld", m->card, m->mixer, cur, min);
        cur = min;
    }

    if (cur > max) {
        LOG_WARN(
            "%s,%s: current volume is greater than the indicated maximum: "
            "%ld > %ld", m->card, m->mixer, cur, max);
        cur = max;
    }

    assert(cur >= min);
    assert(cur <= max);

    LOG_DBG("muted=%d, cur=%ld, min=%ld, max=%ld", !unmuted, cur, min, max);

    mtx_lock(&mod->lock);
    m->vol_min = min;
    m->vol_max = max;
    m->vol_cur = cur;
    m->online = true;
    m->muted = !unmuted;
    mtx_unlock(&mod->lock);

    mod->bar->refresh(mod->bar);
}

enum run_state {
    RUN_ERROR,
    RUN_FAILED_CONNECT,
    RUN_DISCONNECTED,
    RUN_DONE,
};

static enum run_state
run_while_online(struct module *mod)
{
    struct private *m = mod->private;
    enum run_state ret = RUN_ERROR;

    snd_mixer_t *handle;
    if (snd_mixer_open(&handle, 0) != 0) {
        LOG_ERR("failed to open handle");
        return ret;
    }

    if (snd_mixer_attach(handle, m->card) != 0 ||
        snd_mixer_selem_register(handle, NULL, NULL) != 0 ||
        snd_mixer_load(handle) != 0)
    {
        LOG_ERR("failed to attach to card");
        ret = RUN_FAILED_CONNECT;
        goto err;
    }

    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, m->mixer);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem == NULL) {
        LOG_ERR("failed to find mixer");
        goto err;
    }

    /* Get available channels */
    for (size_t i = 0; i < SND_MIXER_SCHN_LAST; i++) {
        if (snd_mixer_selem_has_playback_channel(elem, i)) {
            tll_push_back(m->channels, i);
        }
    }

    char channels_str[1024];
    int channels_idx = 0;
    tll_foreach(m->channels, it) {
        channels_idx += snprintf(
            &channels_str[channels_idx], sizeof(channels_str) - channels_idx,
            channels_idx == 0 ? "%s" : ", %s",
            snd_mixer_selem_channel_name(it->item));
        assert(channels_idx <= sizeof(channels_str));
    }

    LOG_INFO("%s,%s: channels: %s", m->card, m->mixer, channels_str);

    /* Verify volume/muted channel names are valid and exists */
    bool volume_channel_is_valid = m->volume_channel == NULL;
    bool muted_channel_is_valid = m->muted_channel == NULL;

    tll_foreach(m->channels, it) {
        const char *chan_name = snd_mixer_selem_channel_name(it->item);
        if (m->volume_channel != NULL && strcmp(chan_name, m->volume_channel) == 0)
            volume_channel_is_valid = true;
        if (m->muted_channel != NULL && strcmp(chan_name, m->muted_channel) == 0)
            muted_channel_is_valid = true;
    }

    if (!volume_channel_is_valid) {
        assert(m->volume_channel != NULL);
        LOG_ERR("volume: invalid channel name: %s", m->volume_channel);
        goto err;
    }

    if (!muted_channel_is_valid) {
        assert(m->muted_channel != NULL);
        LOG_ERR("muted: invalid channel name: %s", m->muted_channel);
        goto err;
    }

    /* Initial state */
    update_state(mod, elem);

    LOG_INFO("%s,%s: volume min=%ld, max=%ld, current=%ld%s",
             m->card, m->mixer, m->vol_min, m->vol_max, m->vol_cur,
             m->muted ? ", muted" : "");

    mod->bar->refresh(mod->bar);

    while (true) {
        int fd_count = snd_mixer_poll_descriptors_count(handle);
        assert(fd_count >= 1);

        struct pollfd fds[1 + fd_count];

        fds[0] = (struct pollfd){.fd = mod->abort_fd, .events = POLLIN};
        snd_mixer_poll_descriptors(handle, &fds[1], fd_count);

        int r = poll(fds, fd_count + 1, -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("failed to poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            ret = RUN_DONE;
            break;
        }

        for (size_t i = 0; i < fd_count; i++) {
            if (fds[1 + i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                LOG_ERR("disconnected from alsa");

                mtx_lock(&mod->lock);
                m->online = false;
                mtx_unlock(&mod->lock);
                mod->bar->refresh(mod->bar);

                ret = RUN_DISCONNECTED;
                goto err;
            }
        }

        snd_mixer_handle_events(handle);
        update_state(mod, elem);
    }

err:
    snd_mixer_close(handle);
    snd_config_update_free_global();
    return ret;
}

static int
run(struct module *mod)
{
    static const int min_timeout_ms = 500;
    static const int max_timeout_ms = 30000;
    int timeout_ms = min_timeout_ms;

    while (true) {
        enum run_state state = run_while_online(mod);

        switch (state) {
        case RUN_DONE:
            return 0;

        case RUN_ERROR:
            return 1;

        case RUN_FAILED_CONNECT:
            timeout_ms *= 2;
            break;

        case RUN_DISCONNECTED:
            timeout_ms = min_timeout_ms;
            break;
        }

        if (timeout_ms > max_timeout_ms)
            timeout_ms = max_timeout_ms;

        struct timeval now;
        gettimeofday(&now, NULL);

        struct timeval timeout = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };

        struct timeval deadline;
        timeradd(&now, &timeout, &deadline);

        LOG_DBG("timeout is now %dms", timeout_ms);

        while (true) {

            struct timeval n;
            gettimeofday(&n, NULL);

            struct timeval left;
            timersub(&deadline, &n, &left);

            int poll_timeout = timercmp(&left, &(struct timeval){0}, <)
                ? 0
                : left.tv_sec * 1000 + left.tv_usec / 1000;

            LOG_DBG(
                "polling for alsa device to become available (timeout=%dms)",
                poll_timeout);

            struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN}};
            int r = poll(fds, 1, poll_timeout);

            if (r < 0) {
                if (errno == EINTR)
                    continue;

                LOG_ERRNO("failed to poll");
                return 1;
            }

            if (fds[0].revents & POLLIN)
                return 0;

            assert(r == 0);
            break;
        }
    }
}

static struct module *
alsa_new(const char *card, const char *mixer,
         const char *volume_channel, const char *muted_channel,
         struct particle *label)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->label = label;
    priv->card = strdup(card);
    priv->mixer = strdup(mixer);
    priv->volume_channel = volume_channel != NULL ? strdup(volume_channel) : NULL;
    priv->muted_channel = muted_channel != NULL ?  strdup(muted_channel) : NULL;

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
    const struct yml_node *card = yml_get_value(node, "card");
    const struct yml_node *mixer = yml_get_value(node, "mixer");
    const struct yml_node *volume = yml_get_value(node, "volume");
    const struct yml_node *muted = yml_get_value(node, "muted");
    const struct yml_node *content = yml_get_value(node, "content");

    return alsa_new(
        yml_value_as_string(card),
        yml_value_as_string(mixer),
        volume != NULL ? yml_value_as_string(volume) : NULL,
        muted != NULL ? yml_value_as_string(muted) : NULL,
        conf_to_particle(content, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"card", true, &conf_verify_string},
        {"mixer", true, &conf_verify_string},
        {"volume", false, &conf_verify_string},
        {"muted", false, &conf_verify_string},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_alsa_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_alsa_iface")));
#endif
