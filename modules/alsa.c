#include "alsa.h"

#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#define LOG_MODULE "alsa"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar.h"

struct private {
    char *card;
    char *mixer;
    struct particle *label;

    long vol_min;
    long vol_max;
    long vol_cur;
    bool muted;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
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

    long cur, min, max;
    snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &cur);
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

    int unmuted;
    snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &unmuted);

    LOG_DBG("muted=%d, cur=%ld, min=%ld, max=%ld", !unmuted, cur, min, max);

    mtx_lock(&mod->lock);
    m->vol_min = min;
    m->vol_max = max;
    m->vol_cur = cur;
    m->muted = !unmuted;
    mtx_unlock(&mod->lock);

    mod->bar->refresh(mod->bar);
}

static int
run(struct module_run_context *ctx)
{
    struct module *mod = ctx->module;
    struct private *m = mod->private;

    module_signal_ready(ctx);

    snd_mixer_t *handle;
    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, m->card);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, m->mixer);
    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);

    /* Initial state */
    update_state(mod, elem);

    LOG_INFO("%s,%s: volume min=%ld, max=%ld, current=%ld%s",
             m->card, m->mixer, m->vol_min, m->vol_max, m->vol_cur,
             m->muted ? ", muted" : "");

    while (true) {
        int fd_count = snd_mixer_poll_descriptors_count(handle);
        assert(fd_count >= 1);

        struct pollfd fds[1 + fd_count];

        fds[0] = (struct pollfd){.fd = ctx->abort_fd, .events = POLLIN};
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

    snd_mixer_close(handle);
    snd_config_update_free_global();
    return 0;
}

struct module *
module_alsa(const char *card, const char *mixer, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->label = label;
    priv->card = strdup(card);
    priv->mixer = strdup(mixer);
    priv->vol_cur = priv->vol_min = priv->vol_max = 0;
    priv->muted = true;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
