#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>

#include <sys/eventfd.h>

#include <mpd/client.h>

#define LOG_MODULE "mpd"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar.h"
#include "../config.h"

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
    bool repeat;
    bool random;
    bool consume;
    char *album;
    char *artist;
    char *title;

    struct {
        uint64_t value;
        struct timespec when;
    } elapsed;
    uint64_t duration;

    thrd_t refresh_thread_id;
    int refresh_abort_fd;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    if (m->refresh_thread_id != 0) {
        assert(m->refresh_abort_fd != -1);
        write(m->refresh_abort_fd, &(uint64_t){1}, sizeof(uint64_t));

        int res;
        thrd_join(m->refresh_thread_id, &res);
        close(m->refresh_abort_fd);
    };

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

static void
secs_to_str(unsigned secs, char *s, size_t sz)
{
    unsigned hours = secs / (60 * 60);
    unsigned minutes = secs % (60 * 60) / 60;
    secs %= 60;

    if (hours > 0)
        snprintf(s, sz, "%02u:%02u:%02u", hours, minutes, secs);
    else
        snprintf(s, sz, "%02u:%02u", minutes, secs);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    mtx_lock(&mod->lock);

    /* Calculate what 'elapsed' is now */
    uint64_t elapsed = m->elapsed.value;

    if (m->state == STATE_PLAY) {
        elapsed += timespec_diff_milli_seconds(&now, &m->elapsed.when);
        if (elapsed > m->duration) {
            LOG_WARN(
                "dynamic update of elapsed overflowed: "
                "elapsed=%"PRIu64", duration=%"PRIu64, elapsed, m->duration);
            elapsed = m->duration;
        }

    }

    unsigned elapsed_secs = elapsed / 1000;
    unsigned duration_secs = m->duration / 1000;

    /* elapsed/duration as strings (e.g. 03:37) */
    char pos[16], end[16];
    secs_to_str(elapsed_secs, pos, sizeof(pos));
    secs_to_str(duration_secs, end, sizeof(end));

    /* State as string */
    const char *state_str = NULL;
    switch (m->state) {
    case STATE_OFFLINE: state_str = "offline"; break;
    case STATE_STOP:    state_str = "stopped"; break;
    case STATE_PAUSE:   state_str = "paused"; break;
    case STATE_PLAY:    state_str = "playing"; break;
    }

    /* Tell particle to real-time track? */
    enum tag_realtime_unit realtime = m->state == STATE_PLAY
        ? TAG_REALTIME_MSECS : TAG_REALTIME_NONE;

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "state", state_str),
            tag_new_bool(mod, "repeat", m->repeat),
            tag_new_bool(mod, "random", m->random),
            tag_new_bool(mod, "consume", m->consume),
            tag_new_string(mod, "album", m->album),
            tag_new_string(mod, "artist", m->artist),
            tag_new_string(mod, "title", m->title),
            tag_new_string(mod, "pos", pos),
            tag_new_string(mod, "end", end),
            tag_new_int(mod, "duration", m->duration),
            tag_new_int_realtime(
                mod, "elapsed", elapsed, 0, m->duration, realtime),
        },
        .count = 11,
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
        mpd_connection_free(conn);
        return NULL;
    }

    const unsigned *version = mpd_connection_get_server_version(conn);
    LOG_INFO("MPD %u.%u.%u", version[0], version[1], version[2]);

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
    m->repeat = mpd_status_get_repeat(status);
    m->random = mpd_status_get_random(status);
    m->consume = mpd_status_get_consume(status);
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
run(struct module *mod)
{
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

            struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN}};
            int res = poll(fds, 1, 10 * 1000);

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
                {.fd = mod->abort_fd, .events = POLLIN},
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

    /* Extract data from context so that we can free it */
    int abort_fd = ctx->abort_fd;
    long milli_seconds = ctx->milli_seconds;
    free(ctx);

    LOG_DBG("going to sleep for %ldms", milli_seconds);

    /* Wait for timeout, or abort signal */
    struct pollfd fds[] = {{.fd = abort_fd, .events = POLLIN}};
    int r = poll(fds, 1, milli_seconds);

    if (r < 0) {
        LOG_ERRNO("failed to poll() in refresh thread");
        return 1;
    }

    /* Aborted? */
    if (r == 1) {
        assert(fds[0].revents & POLLIN);
        LOG_DBG("refresh thread aborted");
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
    if (m->refresh_thread_id != 0) {
        LOG_DBG("aborting current refresh thread");

        /* Signal abort to thread */
        assert(m->refresh_abort_fd != -1);
        write(m->refresh_abort_fd, &(uint64_t){1}, sizeof(uint64_t));

        /* Wait for it to finish */
        int res;
        thrd_join(m->refresh_thread_id, &res);

        /* Close and cleanup */
        close(m->refresh_abort_fd);
        m->refresh_abort_fd = -1;
        m->refresh_thread_id = 0;
    }

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
    int r = thrd_create(&m->refresh_thread_id, &refresh_in_thread, ctx);

    if (r != 0) {
        LOG_ERR("failed to create refresh thread");
        close(m->refresh_abort_fd);
        m->refresh_abort_fd = -1;
        m->refresh_thread_id = 0;
        free(ctx);
    }

    /* Detach - we don't want to have to thrd_join() it */
    //thrd_detach(tid);
    return r == 0;
}

static struct module *
mpd_new(const char *host, uint16_t port, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->host = strdup(host);
    priv->port = port;
    priv->label = label;
    priv->conn = NULL;
    priv->state = STATE_OFFLINE;
    priv->repeat = priv->random = priv->consume = false;
    priv->album = NULL;
    priv->artist = NULL;
    priv->title = NULL;
    priv->elapsed.value = 0;
    priv->elapsed.when.tv_sec = priv->elapsed.when.tv_nsec = 0;
    priv->duration = 0;
    priv->refresh_thread_id = 0;
    priv->refresh_abort_fd = -1;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->refresh_in = &refresh_in;
    return mod;
}

struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *host = yml_get_value(node, "host");
    const struct yml_node *port = yml_get_value(node, "port");
    const struct yml_node *c = yml_get_value(node, "content");

    return mpd_new(
        yml_value_as_string(host),
        port != NULL ? yml_value_as_int(port) : 0,
        conf_to_particle(c, inherited));
}

bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"host", true, &conf_verify_string},
        {"port", false, &conf_verify_int},
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}
