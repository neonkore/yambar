#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#define LOG_MODULE "alsa"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../plugin.h"
#include "../tllist.h"

struct private {
    char *card;
    char *mixer;
    struct particle *label;

    tll(snd_mixer_selem_channel_id_t) channels;

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
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);
    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_int_range(mod, "volume", m->vol_cur, m->vol_min, m->vol_max),
            tag_new_bool(mod, "muted", m->muted),
        },
        .count = 2,
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

    int idx = 0;

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

    long cur[tll_length(m->channels)];
    memset(cur, 0, sizeof(cur));

    /* If volume level can be changed (i.e. this isn't just a switch;
     * e.g. a digital channel), get current level */
    if (max > 0) {
        tll_foreach(m->channels, it) {
            int r = snd_mixer_selem_get_playback_volume(
                elem, it->item, &cur[idx]);

            if (r < 0) {
                LOG_WARN("%s,%s: %s: failed to get current volume",
                         m->card, m->mixer,
                         snd_mixer_selem_channel_name(it->item));
            }

            LOG_DBG("%s,%s: %s: volume: %ld", m->card, m->mixer,
                    snd_mixer_selem_channel_name(it->item), cur[idx]);
            idx++;
        }
    }

    int unmuted[tll_length(m->channels)];
    memset(unmuted, 0, sizeof(unmuted));

    /* Get muted state */
    idx = 0;
    tll_foreach(m->channels, it) {
        int r = snd_mixer_selem_get_playback_switch(
            elem, it->item, &unmuted[idx]);

        if (r < 0) {
            LOG_WARN("%s,%s: %s: failed to get muted state",
                     m->card, m->mixer, snd_mixer_selem_channel_name(it->item));
        }

        LOG_DBG("%s,%s: %s: muted: %d", m->card, m->mixer,
                snd_mixer_selem_channel_name(it->item), !unmuted[idx]);

        idx++;
    }

    /* Warn if volume level is inconsistent across the channels */
    for (size_t i = 1; i < tll_length(m->channels); i++) {
        if (cur[i] != cur[i - 1]) {
            LOG_WARN("%s,%s: channel volume mismatch, using value from %s",
                     m->card, m->mixer,
                     snd_mixer_selem_channel_name(tll_front(m->channels)));
            break;
        }
    }

    /* Warn if muted state is inconsistent across the channels */
    for (size_t i = 1; i < tll_length(m->channels); i++) {
        if (unmuted[i] != unmuted[i - 1]) {
            LOG_WARN("%s,%s: channel muted mismatch, using value from %s",
                     m->card, m->mixer,
                     snd_mixer_selem_channel_name(tll_front(m->channels)));
            break;
        }
    }

    /* Make sure min <= cur <= max */
    if (cur[0] < min) {
        LOG_WARN(
            "%s,%s: current volume is less than the indicated minimum: "
            "%ld < %ld", m->card, m->mixer, cur[0], min);
        cur[0] = min;
    }

    if (cur[0] > max) {
        LOG_WARN(
            "%s,%s: current volume is greater than the indicated maximum: "
            "%ld > %ld", m->card, m->mixer, cur[0], max);
        cur[0] = max;
    }

    assert(cur[0] >= min);
    assert(cur[0] <= max);

    LOG_DBG(
        "muted=%d, cur=%ld, min=%ld, max=%ld", !unmuted[0], cur[0], min, max);

    mtx_lock(&mod->lock);
    m->vol_min = min;
    m->vol_max = max;
    m->vol_cur = cur[0];
    m->muted = !unmuted[0];
    mtx_unlock(&mod->lock);

    mod->bar->refresh(mod->bar);
}

static int
run(struct module *mod)
{
    struct private *m = mod->private;
    int ret = 1;

    snd_mixer_t *handle;
    if (snd_mixer_open(&handle, 0) != 0) {
        LOG_ERR("failed to open handle");
        return 1;
    }

    if (snd_mixer_attach(handle, m->card) != 0 ||
        snd_mixer_selem_register(handle, NULL, NULL) != 0 ||
        snd_mixer_load(handle) != 0)
    {
        LOG_ERR("failed to attach to card");
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

        poll(fds, fd_count + 1, -1);

        if (fds[0].revents & POLLIN)
            break;

        if (fds[1].revents & POLLHUP) {
            /* Don't know if this can happen */
            LOG_ERR("disconnected from alsa");
            break;
        }

        snd_mixer_handle_events(handle);
        update_state(mod, elem);
    }

    ret = 0;

err:
    snd_mixer_close(handle);
    snd_config_update_free_global();
    return ret;
}

static struct module *
alsa_new(const char *card, const char *mixer, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->label = label;
    priv->card = strdup(card);
    priv->mixer = strdup(mixer);
    memset(&priv->channels, 0, sizeof(priv->channels));
    priv->vol_cur = priv->vol_min = priv->vol_max = 0;
    priv->muted = true;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *card = yml_get_value(node, "card");
    const struct yml_node *mixer = yml_get_value(node, "mixer");
    const struct yml_node *content = yml_get_value(node, "content");

    return alsa_new(
        yml_value_as_string(card),
        yml_value_as_string(mixer),
        conf_to_particle(content, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"card", true, &conf_verify_string},
        {"mixer", true, &conf_verify_string},
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
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
