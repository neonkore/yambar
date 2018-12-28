#include "mpd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>

#include <sys/eventfd.h>

#include <mpd/client.h>

#define LOG_MODULE "mpd"
#define LOG_ENABLE_DBG 0
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

    struct {
        uint64_t value;
        struct timespec when;
    } elapsed;
    uint64_t duration;

    int refresh_abort_fd;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    if (m->refresh_abort_fd != -1)
        write(m->refresh_abort_fd, &(uint64_t){1}, sizeof(uint64_t));

    free(m->host);
    free(m->album);
    free(m->artist);
    free(m->title);
    assert(m->conn == NULL);

    m->label->destroy(m->label);

    free(m);
    module_default_destroy(mod);
}

static uint64_t
timespec_diff_milli_seconds(const struct timespec *a, const struct timespec *b)
{
    /* TODO */
    uint64_t nsecs_a = a->tv_sec * 1000000000 + a->tv_nsec;
    uint64_t nsecs_b = b->tv_sec * 1000000000 + b->tv_nsec;

    assert(nsecs_a >= nsecs_b);
    uint64_t nsec_diff = nsecs_a - nsecs_b;
    return nsec_diff / 1000000;
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    /* Calculate what elapsed is now */

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t elapsed = m->elapsed.value +
        timespec_diff_milli_seconds(&now, &m->elapsed.when);

    unsigned elapsed_secs = elapsed / 1000;
    unsigned duration_secs = m->duration / 1000;

    mtx_lock(&mod->lock);

    const char *state_str = NULL;
    switch (m->state) {
    case STATE_OFFLINE: state_str = "offline"; break;
    case STATE_STOP:    state_str = "stopped"; break;
    case STATE_PAUSE:   state_str = "paused"; break;
    case STATE_PLAY:    state_str = "playing"; break;
    }

    char pos[16], end[16];

    if (elapsed_secs >= 60 * 60)
        snprintf(pos, sizeof(pos), "%02u:%02u:%02u",
                 elapsed_secs / (60 * 60),
                 elapsed_secs % (60 * 60) / 60,
                 elapsed_secs % 60);
    else
        snprintf(pos, sizeof(pos), "%02u:%02u",
                 elapsed_secs / 60, elapsed_secs % 60);

    if (duration_secs >= 60 * 60)
        snprintf(end, sizeof(end), "%02u:%02u:%02u",
                 duration_secs / (60 * 60),
                 duration_secs % (60 * 60) / 60,
                 duration_secs % 60);
    else
        snprintf(end, sizeof(end), "%02u:%02u",
                 duration_secs / 60, duration_secs % 60);

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "state", state_str),
            tag_new_string(mod, "album", m->album),
            tag_new_string(mod, "artist", m->artist),
            tag_new_string(mod, "title", m->title),
            tag_new_string(mod, "pos", pos),
            tag_new_string(mod, "end", end),
            tag_new_int(mod, "duration", m->duration),
            tag_new_int_realtime(
                mod, "elapsed", elapsed, 0, m->duration,
                m->state == STATE_PLAY ? TAG_REALTIME_MSECS : TAG_REALTIME_NONE),
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

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    mtx_lock(&mod->lock);
    m->state = mpd_status_get_state(status);
    m->duration = mpd_status_get_total_time(status) * 1000;
    m->elapsed.value = mpd_status_get_elapsed_ms(status);
    m->elapsed.when = now;
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
        free(m->album);
        free(m->artist);
        free(m->title);

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
        m->elapsed.value = m->duration = 0;
        m->elapsed.when.tv_sec = m->elapsed.when.tv_nsec = 0;
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

            if (fds[1].revents & POLLIN) {
                enum mpd_idle idle __attribute__ ((unused)) =
                    mpd_recv_idle(m->conn, true);

                LOG_DBG("IDLE mask: %d", idle);

                if (!update_status(mod))
                    break;

                bar->refresh(bar);
            }
        }
    }

    if (m->conn != NULL) {
        mpd_connection_free(m->conn);
        m->conn = NULL;
    }

    return 0;
}

struct refresh_context {
    struct module *mod;
    int abort_fd;
    long milli_seconds;
};

static int
refresh_in_thread(void *arg)
{
    struct refresh_context *ctx = arg;
    struct module *mod = ctx->mod;
    struct private *m = mod->private;

    /* Extract data from context so that we can free it */
    int abort_fd = ctx->abort_fd;
    long milli_seconds = ctx->milli_seconds;
    free(ctx);

    LOG_DBG("going to sleep for %ldms", milli_seconds);

    /* Wait for timeout, or abort signal */
    struct pollfd fds[] = {{.fd = abort_fd, .events = POLLIN}};
    int r = poll(fds, 1, milli_seconds);

    /* Close abort eventfd */
    mtx_lock(&mod->lock);
    close(abort_fd);
    if (m->refresh_abort_fd == abort_fd)
        m->refresh_abort_fd = -1;
    mtx_unlock(&mod->lock);

    /* Aborted? */
    if (r == 1) {
        assert(fds[0].revents & POLLIN);
        LOG_DBG("aborted");
        return 0;
    }

    LOG_DBG("timed refresh");
    mod->bar->refresh(mod->bar);

    return 0;
}

static bool
refresh_in(struct module *mod, long milli_seconds)
{
    struct private *m = mod->private;

    /* Abort currently running refresh thread */
    mtx_lock(&mod->lock);
    if (m->refresh_abort_fd != -1) {
        LOG_DBG("aborting current refresh thread");
        write(m->refresh_abort_fd, &(uint64_t){1}, sizeof(uint64_t));

        /* Closed by thread */
        m->refresh_abort_fd = -1;
    }
    mtx_unlock(&mod->lock);

    /* Create a new eventfd, to be able to signal abort to the thread */
    int abort_fd = eventfd(0, EFD_CLOEXEC);
    if (abort_fd == -1) {
        LOG_ERRNO("failed to create eventfd");
        return false;
    }

    /* Thread context */
    struct refresh_context *ctx = malloc(sizeof(*ctx));
    ctx->mod = mod;
    ctx->abort_fd = m->refresh_abort_fd = abort_fd;
    ctx->milli_seconds = milli_seconds;

    /* Create thread */
    thrd_t tid;
    int r = thrd_create(&tid, &refresh_in_thread, ctx);
    if (r != 0)
        LOG_ERR("failed to create refresh thread");

    /* Detach - we don't want to have to thrd_join() it */
    thrd_detach(tid);
    return r == 0;
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
    priv->elapsed.value = 0;
    priv->elapsed.when.tv_sec = priv->elapsed.when.tv_nsec = 0;
    priv->duration = 0;
    priv->refresh_abort_fd = -1;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->refresh_in = &refresh_in;
    return mod;
}
