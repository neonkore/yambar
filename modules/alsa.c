#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/inotify.h>

#include <alsa/asoundlib.h>

#include <tllist.h>

#define LOG_MODULE "alsa"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../plugin.h"

enum channel_type { CHANNEL_PLAYBACK, CHANNEL_CAPTURE };

struct channel {
    snd_mixer_selem_channel_id_t id;
    enum channel_type type;
    char *name;

    bool use_db;
    long vol_cur;
    long db_cur;
    bool muted;
};

struct private {
    char *card;
    char *mixer;
    char *volume_name;
    char *muted_name;
    struct particle *label;

    tll(struct channel) channels;

    bool online;

    bool has_playback_volume;
    long playback_vol_min;
    long playback_vol_max;

    bool has_playback_db;
    long playback_db_min;
    long playback_db_max;

    bool has_capture_volume;
    long capture_vol_min;
    long capture_vol_max;

    long has_capture_db;
    long capture_db_min;
    long capture_db_max;

    const struct channel *volume_chan;
    const struct channel *muted_chan;
};

static void
channel_free(struct channel *chan)
{
    free(chan->name);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    tll_foreach(m->channels, it) {
        channel_free(&it->item);
        tll_remove(m->channels, it);
    }
    m->label->destroy(m->label);
    free(m->card);
    free(m->mixer);
    free(m->volume_name);
    free(m->muted_name);
    free(m);
    module_default_destroy(mod);
}

static const char *
description(const struct module *mod)
{
    static char desc[32];
    const struct private *m = mod->private;
    snprintf(desc, sizeof(desc), "alsa(%s)", m->card);
    return desc;
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const struct channel *volume_chan = m->volume_chan;
    const struct channel *muted_chan = m->muted_chan;

    bool muted = muted_chan != NULL ? muted_chan->muted : false;
    long vol_min = 0, vol_max = 0, vol_cur = 0;
    long db_min = 0, db_max = 0, db_cur = 0;
    bool use_db = false;

    if (volume_chan != NULL) {
        if (volume_chan->type == CHANNEL_PLAYBACK) {
            db_min = m->playback_db_min;
            db_max = m->playback_db_max;
            vol_min = m->playback_vol_min;
            vol_max = m->playback_vol_max;
        } else {
            db_min = m->capture_db_min;
            db_max = m->capture_db_max;
            vol_min = m->capture_vol_min;
            vol_max = m->capture_vol_max;
        }
        vol_cur = volume_chan->vol_cur;
        db_cur = volume_chan->db_cur;
        use_db = volume_chan->use_db;
    }

    int percent;

    if (use_db) {
        bool use_linear = db_max - db_min <= 24 * 100;
        if (use_linear) {
            percent = db_min - db_max > 0
                ? round(100. * (db_cur - db_min) / (db_max - db_min))
                : 0;
        } else {
            double normalized = pow(10, (double)(db_cur - db_max) / 6000.);
            if (db_min != SND_CTL_TLV_DB_GAIN_MUTE) {
                double min_norm = pow(10, (double)(db_min - db_max) / 6000.);
                normalized = (normalized - min_norm) / (1. - min_norm);
            }
            percent = round(100. * normalized);
        }
    } else {
        percent = vol_max - vol_min > 0
            ? round(100. * (vol_cur - vol_min) / (vol_max - vol_min))
            : 0;
    }

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_bool(mod, "online", m->online),
            tag_new_int_range(mod, "volume", vol_cur, vol_min, vol_max),
            tag_new_int_range(mod, "dB", db_cur, db_min, db_max),
            tag_new_int_range(mod, "percent", percent, 0, 100),
            tag_new_bool(mod, "muted", muted),
        },
        .count = 5,
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

    mtx_lock(&mod->lock);

    /* If volume level can be changed (i.e. this isn't just a switch;
     * e.g. a digital channel), get current channel levels */
    tll_foreach(m->channels, it) {
        struct channel *chan = &it->item;

        const bool has_volume = chan->type == CHANNEL_PLAYBACK
            ? m->has_playback_volume : m->has_capture_volume;
        const bool has_db = chan->type == CHANNEL_PLAYBACK
            ? m->has_playback_db : m->has_capture_db;

        if (!has_volume && !has_db)
            continue;


        if (has_db) {
            chan->use_db = true;

            const long min = chan->type == CHANNEL_PLAYBACK
                ? m->playback_db_min : m->capture_db_min;
            const long max = chan->type == CHANNEL_PLAYBACK
                ? m->playback_db_max : m->capture_db_max;
            assert(min <= max);

            int r = chan->type == CHANNEL_PLAYBACK
                ? snd_mixer_selem_get_playback_dB(elem, chan->id, &chan->db_cur)
                : snd_mixer_selem_get_capture_dB(elem, chan->id, &chan->db_cur);

            if (r < 0) {
                LOG_ERR("%s,%s: %s: failed to get current dB",
                        m->card, m->mixer, chan->name);
            }

            if (chan->db_cur < min) {
                LOG_WARN(
                    "%s,%s: %s: current dB is less than the indicated minimum: "
                    "%ld < %ld", m->card, m->mixer, chan->name, chan->db_cur, min);
                chan->db_cur = min;
            }

            if (chan->db_cur > max) {
                LOG_WARN(
                    "%s,%s: %s: current dB is greater than the indicated maximum: "
                    "%ld > %ld", m->card, m->mixer, chan->name, chan->db_cur, max);
                chan->db_cur = max;
            }

            assert(chan->db_cur >= min);
            assert(chan->db_cur <= max );

            LOG_DBG("%s,%s: %s: dB: %ld",
                    m->card, m->mixer, chan->name, chan->db_cur);
        } else
            chan->use_db = false;

        const long min = chan->type == CHANNEL_PLAYBACK
            ? m->playback_vol_min : m->capture_vol_min;
        const long max = chan->type == CHANNEL_PLAYBACK
            ? m->playback_vol_max : m->capture_vol_max;
        assert(min <= max);

        int r = chan->type == CHANNEL_PLAYBACK
            ? snd_mixer_selem_get_playback_volume(elem, chan->id, &chan->vol_cur)
            : snd_mixer_selem_get_capture_volume(elem, chan->id, &chan->vol_cur);

        if (r < 0) {
            LOG_ERR("%s,%s: %s: failed to get current volume",
                    m->card, m->mixer, chan->name);
        }

        if (chan->vol_cur < min) {
            LOG_WARN(
                "%s,%s: %s: current volume is less than the indicated minimum: "
                "%ld < %ld", m->card, m->mixer, chan->name, chan->vol_cur, min);
            chan->vol_cur = min;
        }

        if (chan->vol_cur > max) {
            LOG_WARN(
                "%s,%s: %s: current volume is greater than the indicated maximum: "
                "%ld > %ld", m->card, m->mixer, chan->name, chan->vol_cur, max);
            chan->vol_cur = max;
        }

        assert(chan->vol_cur >= min);
        assert(chan->vol_cur <= max );

        LOG_DBG("%s,%s: %s: volume: %ld",
                m->card, m->mixer, chan->name, chan->vol_cur);
    }

    /* Get channels’ muted state */
    tll_foreach(m->channels, it) {
        struct channel *chan = &it->item;

        int unmuted;

        int r = chan->type == CHANNEL_PLAYBACK
            ? snd_mixer_selem_get_playback_switch(elem, chan->id, &unmuted)
            : snd_mixer_selem_get_capture_switch(elem, chan->id, &unmuted);

        if (r < 0) {
            LOG_WARN("%s,%s: %s: failed to get muted state",
                     m->card, m->mixer, chan->name);
            unmuted = 1;
        }

        chan->muted = !unmuted;
        LOG_DBG("%s,%s: %s: muted: %d", m->card, m->mixer, chan->name, !unmuted);
    }

    m->online = true;

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

    /* Make sure we aren’t still tracking channels from previous connects */
    tll_free(m->channels);

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

    /* Get playback volume range */
    m->has_playback_volume = snd_mixer_selem_has_playback_volume(elem) > 0;
    if (m->has_playback_volume) {
        if (snd_mixer_selem_get_playback_volume_range(
                elem, &m->playback_vol_min, &m->playback_vol_max) < 0)
        {
            LOG_ERR("%s,%s: failed to get playback volume range",
                    m->card, m->mixer);
            assert(m->playback_vol_min == 0);
            assert(m->playback_vol_max == 0);
        }

        if (m->playback_vol_min > m->playback_vol_max) {
            LOG_WARN(
                "%s,%s: indicated minimum playback volume is greater than the "
                "maximum: %ld > %ld",
                m->card, m->mixer, m->playback_vol_min, m->playback_vol_max);
            m->playback_vol_min = m->playback_vol_max;
        }
    }

    if (snd_mixer_selem_get_playback_dB_range(
            elem, &m->playback_db_min, &m->playback_db_max) < 0)
    {
        LOG_WARN(
            "%s,%s: failed to get playback dB range, "
            "will use raw volume values instead", m->card, m->mixer);
        m->has_playback_db = false;
    } else
        m->has_playback_db = true;

    /* Get capture volume range */
    m->has_capture_volume = snd_mixer_selem_has_capture_volume(elem) > 0;
    if (m->has_capture_volume) {
        if (snd_mixer_selem_get_capture_volume_range(
                elem, &m->capture_vol_min, &m->capture_vol_max) < 0)
        {
            LOG_ERR("%s,%s: failed to get capture volume range",
                    m->card, m->mixer);
            assert(m->capture_vol_min == 0);
            assert(m->capture_vol_max == 0);
        }

        if (m->capture_vol_min > m->capture_vol_max) {
            LOG_WARN(
                "%s,%s: indicated minimum capture volume is greater than the "
                "maximum: %ld > %ld",
                m->card, m->mixer, m->capture_vol_min, m->capture_vol_max);
            m->capture_vol_min = m->capture_vol_max;
        }
    }

    if (snd_mixer_selem_get_capture_dB_range(
            elem, &m->capture_db_min, &m->capture_db_max) < 0)
    {
        LOG_WARN(
            "%s,%s: failed to get capture dB range, "
            "will use raw volume values instead", m->card, m->mixer);
        m->has_capture_db = false;
    } else
        m->has_capture_db = true;

    /* Get available channels */
    for (size_t i = 0; i < SND_MIXER_SCHN_LAST; i++) {
        bool is_playback = snd_mixer_selem_has_playback_channel(elem, i) == 1;
        bool is_capture = snd_mixer_selem_has_capture_channel(elem, i) == 1;

        if (is_playback || is_capture) {
            struct channel chan = {
                .id = i,
                .type = is_playback ? CHANNEL_PLAYBACK : CHANNEL_CAPTURE,
                .name = strdup(snd_mixer_selem_channel_name( i)),
            };
            tll_push_back(m->channels, chan);
        }
    }

    if (tll_length(m->channels) == 0) {
        LOG_ERR("%s,%s: no channels", m->card, m->mixer);
        goto err;
    }

    char channels_str[1024];
    int channels_idx = 0;
    tll_foreach(m->channels, it) {
        const struct channel *chan = &it->item;

        channels_idx += snprintf(
            &channels_str[channels_idx], sizeof(channels_str) - channels_idx,
            channels_idx == 0 ? "%s (%s)" : ", %s (%s)",
            chan->name, chan->type == CHANNEL_PLAYBACK ? "🔊" : "🎤");
        assert(channels_idx <= sizeof(channels_str));
    }

    LOG_INFO("%s,%s: channels: %s", m->card, m->mixer, channels_str);

    /* Verify volume/muted channel names are valid and exists */
    bool volume_channel_is_valid = m->volume_name == NULL;
    bool muted_channel_is_valid = m->muted_name == NULL;

    tll_foreach(m->channels, it) {
        const struct channel *chan = &it->item;
        if (m->volume_name != NULL && strcmp(chan->name, m->volume_name) == 0) {
            m->volume_chan = chan;
            volume_channel_is_valid = true;
        }
        if (m->muted_name != NULL && strcmp(chan->name, m->muted_name) == 0) {
            m->muted_chan = chan;
            muted_channel_is_valid = true;
        }
    }

    if (m->volume_name == NULL)
        m->volume_chan = &tll_front(m->channels);
    if (m->muted_name == NULL)
        m->muted_chan = &tll_front(m->channels);

    if (!volume_channel_is_valid) {
        assert(m->volume_name != NULL);
        LOG_ERR("volume: invalid channel name: %s", m->volume_name);
        goto err;
    }

    if (!muted_channel_is_valid) {
        assert(m->muted_name != NULL);
        LOG_ERR("muted: invalid channel name: %s", m->muted_name);
        goto err;
    }

    /* Initial state */
    update_state(mod, elem);

    LOG_INFO(
        "%s,%s: %s range=%ld-%ld, current=%ld%s (sources: volume=%s, muted=%s)",
        m->card, m->mixer,
        m->volume_chan->use_db ? "dB" : "volume",
        (m->volume_chan->type == CHANNEL_PLAYBACK
         ? (m->volume_chan->use_db
            ? m->playback_db_min
            : m->playback_vol_min)
         : (m->volume_chan->use_db
            ? m->capture_db_min
            : m->capture_vol_min)),
        (m->volume_chan->type == CHANNEL_PLAYBACK
         ? (m->volume_chan->use_db
            ? m->playback_db_max
            : m->playback_vol_max)
         : (m->volume_chan->use_db
            ? m->capture_db_max
            : m->capture_vol_max)),
        m->volume_chan->use_db ? m->volume_chan->db_cur : m->volume_chan->vol_cur,
        m->muted_chan->muted ? " (muted)" : "",
        m->volume_chan->name, m->muted_chan->name);

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
    int ret = 1;

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd < 0) {
        LOG_ERRNO("failed to inotify");
        return 1;
    }

    int wd = inotify_add_watch(ifd, "/dev/snd", IN_CREATE);
    if (wd < 0) {
        LOG_ERRNO("failed to create inotify watcher for /dev/snd");
        close(ifd);
        return 1;
    }

    while (true) {
        enum run_state state = run_while_online(mod);

        switch (state) {
        case RUN_DONE:
            ret = 0;
            goto out;

        case RUN_ERROR:
            ret = 1;
            goto out;

        case RUN_FAILED_CONNECT:
            break;

        case RUN_DISCONNECTED:
            /*
             * We’ve been connected - drain the watcher
             *
             * We don’t want old, un-releated events (for other
             * soundcards, for example) to trigger a storm of
             * re-connect attempts.
             */
            while (true) {
                uint8_t buf[1024];
                ssize_t amount = read(ifd, buf, sizeof(buf));
                if (amount < 0) {
                    if (errno == EAGAIN)
                        break;

                    LOG_ERRNO("failed to drain inotify watcher");
                    ret = 1;
                    goto out;
                }

                if (amount == 0)
                    break;
            }

            break;
        }

        bool have_create_event = false;

        while (!have_create_event) {
            struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN},
                                   {.fd = ifd, .events = POLLIN}};
            int r = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);

            if (r < 0) {
                if (errno == EINTR)
                    continue;

                LOG_ERRNO("failed to poll");
                ret = 1;
                goto out;
            }

            if (fds[0].revents & (POLLIN | POLLHUP)) {
                ret = 0;
                goto out;
            }

            if (fds[1].revents & POLLHUP) {
                LOG_ERR("inotify socket closed");
                ret = 1;
                goto out;
            }

            assert(fds[1].revents & POLLIN);

            while (true) {
                char buf[1024];
                ssize_t len = read(ifd, buf, sizeof(buf));

                if (len < 0) {
                    if (errno == EAGAIN)
                        break;

                    LOG_ERRNO("failed to read inotify events");
                    ret = 1;
                    goto out;
                }

                if (len == 0)
                    break;

                /* Consume inotify data */
                for (const char *ptr = buf; ptr < buf + len; ) {
                    const struct inotify_event *e = (const struct inotify_event *)ptr;

                    if (e->mask & IN_CREATE) {
                        LOG_DBG("inotify: CREATED: /dev/snd/%.*s", e->len, e->name);
                        have_create_event = true;
                    }

                    ptr += sizeof(*e) + e->len;
                }
            }
        }
    }

out:
    if (wd >= 0)
        inotify_rm_watch(ifd, wd);
    if (ifd >= 0)
        close (ifd);
    return ret;
}

static struct module *
alsa_new(const char *card, const char *mixer,
         const char *volume_channel_name, const char *muted_channel_name,
         struct particle *label)
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->label = label;
    priv->card = strdup(card);
    priv->mixer = strdup(mixer);
    priv->volume_name =
        volume_channel_name != NULL ? strdup(volume_channel_name) : NULL;
    priv->muted_name =
        muted_channel_name != NULL ?  strdup(muted_channel_name) : NULL;

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
