#include "mpd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <poll.h>

#include <mpd/client.h>

#define LOG_MODULE "mpd"
#define LOG_ENABLE_DBG 1
#include "../../log.h"
#include "../../bar.h"

enum state {
    STATE_OFFLINE = 1000,
    STATE_STOP = MPD_STATE_STOP,
    STATE_PAUSE = MPD_STATE_PAUSE,
    STATE_PLAY = MPD_STATE_PLAY,
};

struct private {
    char *host;
    uint16_t port;
    struct particle *label;

    struct mpd_connection *conn;
    enum state state;
    char *album;
    char *artist;
    char *title;

    unsigned elapsed;
    unsigned duration;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    free(m->host);
    free(m->album);
    free(m->artist);
    free(m->title);
    assert(m->conn == NULL);

    m->label->destroy(m->label);

    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const char *state_str = NULL;
    switch (m->state) {
    case STATE_OFFLINE: state_str = "offline"; break;
    case STATE_STOP:    state_str = "stopped"; break;
    case STATE_PAUSE:   state_str = "paused"; break;
    case STATE_PLAY:    state_str = "playing"; break;
    }

    char pos[16], end[16];

    if (m->elapsed >= 60 * 60)
        snprintf(pos, sizeof(pos), "%02u:%02u:%02u",
                 m->elapsed / (60 * 60),
                 m->elapsed % (60 * 60) / 60,
                 m->elapsed % 60);
    else
        snprintf(pos, sizeof(pos), "%02u:%02u",
                 m->elapsed / 60, m->elapsed % 60);

    if (m->duration >= 60 * 60)
        snprintf(end, sizeof(end), "%02u:%02u:%02u",
                 m->duration / (60 * 60),
                 m->duration % (60 * 60) / 60,
                 m->duration % 60);
    else
        snprintf(end, sizeof(end), "%02u:%02u",
                 m->duration / 60, m->duration % 60);

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string("state", state_str),
            tag_new_string("album", m->album),
            tag_new_string("artist", m->artist),
            tag_new_string("title", m->title),
            tag_new_string("pos", pos),
            tag_new_string("end", end),
            tag_new_int("duration", m->duration),
            tag_new_int_range("elapsed", m->elapsed, 0, m->duration),
        },
        .count = 8,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static struct mpd_connection *
connect_to_mpd(const struct module *mod)
{
    const struct private *m = mod->private;

    struct mpd_connection *conn = mpd_connection_new(m->host, m->port, 0);
    if (conn == NULL) {
        LOG_ERR("failed to create MPD connection");
        return NULL;
    }

    enum mpd_error merr = mpd_connection_get_error(conn);
    if (merr != MPD_ERROR_SUCCESS) {
        LOG_WARN("failed to connect to MPD: %s",
                 mpd_connection_get_error_message(conn));
        return NULL;
    }

    const unsigned *version = mpd_connection_get_server_version(conn);
    LOG_INFO("connected to MPD %u.%u.%u", version[0], version[1], version[2]);

    return conn;
}

static bool
update_status(struct module *mod)
{
    struct private *m = mod->private;

    struct mpd_status *status = mpd_run_status(m->conn);
    if (status == NULL) {
        LOG_ERR("failed to get status: %s",
                mpd_connection_get_error_message(m->conn));
        return false;
    }

    mtx_lock(&mod->lock);
    m->state = mpd_status_get_state(status);
    m->duration = mpd_status_get_total_time(status);
    m->elapsed = mpd_status_get_elapsed_time(status);
    mtx_unlock(&mod->lock);

    mpd_status_free(status);

    struct mpd_song *song = mpd_run_current_song(m->conn);
    if (song == NULL && mpd_connection_get_error(m->conn) != MPD_ERROR_SUCCESS) {
        LOG_ERR("failed to get current song: %s",
                mpd_connection_get_error_message(m->conn));
        return false;
    }

    if (song == NULL) {
        mtx_lock(&mod->lock);
        free(m->album); m->album = NULL;
        free(m->artist); m->artist = NULL;
        free(m->title); m->title = NULL;
        mtx_unlock(&mod->lock);
    } else {
        const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
        const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
        const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);

        mtx_lock(&mod->lock);
        m->album = strdup(album);
        m->artist = strdup(artist);
        m->title = strdup(title);
        mtx_unlock(&mod->lock);

        mpd_song_free(song);
    }

    return true;
}

static int
run(struct module_run_context *ctx)
{
    module_signal_ready(ctx);

    struct module *mod = ctx->module;
    const struct bar *bar = mod->bar;
    struct private *m = mod->private;

    bool aborted = false;

    while (!aborted) {

        if (m->conn != NULL) {
            mpd_connection_free(m->conn);
            m->conn = NULL;
        }

        /* Reset state */
        mtx_lock(&mod->lock);
        free(m->album); m->album = NULL;
        free(m->artist); m->artist = NULL;
        free(m->title); m->title = NULL;
        m->state = STATE_OFFLINE;
        m->elapsed = m->duration = 0;
        mtx_unlock(&mod->lock);

        /* Keep trying to connect, until we succeed */
        while (!aborted) {
            m->conn = connect_to_mpd(mod);
            if (m->conn != NULL)
                break;

            struct pollfd fds[] = {{.fd = ctx->abort_fd, .events = POLLIN}};
            int res = poll(fds, 1, 1 * 1000);

            if (res == 1) {
                assert(fds[0].revents & POLLIN);
                aborted = true;
            }
        }

        if (aborted)
            break;

        /* Initial state (after establishing a connection) */
        assert(m->conn != NULL);
        if (!update_status(mod))
            continue;

        bar->refresh(bar);

        /* Monitor for events from MPD */
        while (true) {
            struct pollfd fds[] = {
                {.fd = ctx->abort_fd, .events = POLLIN},
                {.fd = mpd_connection_get_fd(m->conn), .events = POLLIN},
            };

            if (!mpd_send_idle(m->conn)) {
                LOG_ERR("failed to send IDLE command: %s",
                        mpd_connection_get_error_message(m->conn));
                break;
            }

            poll(fds, 2, -1);

            if (fds[0].revents & POLLIN) {
                aborted = true;
                break;
            }

            if (fds[1].revents & POLLHUP) {
                LOG_WARN("disconnected from MPD daemon");
                break;
            }

            enum mpd_idle idle = mpd_recv_idle(m->conn, true);
            LOG_DBG("IDLE mask: %d", idle);

            if (!update_status(mod))
                break;

            bar->refresh(bar);
        }
    }

    if (m->conn != NULL) {
        mpd_connection_free(m->conn);
        m->conn = NULL;
    }

    return 0;
}

struct module *
module_mpd(const char *host, uint16_t port, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->host = strdup(host);
    priv->port = port;
    priv->label = label;
    priv->conn = NULL;
    priv->state = STATE_OFFLINE;
    priv->album = NULL;
    priv->artist = NULL;
    priv->title = NULL;
    priv->elapsed = 0;
    priv->duration = 0;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
