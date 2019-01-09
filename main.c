#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include <threads.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/eventfd.h>
#include <pwd.h>

#include "bar.h"
#include "config.h"
#include "yml.h"
#include "xcb.h"

#define LOG_MODULE "main"
#include "log.h"

static volatile sig_atomic_t aborted = 0;

static void
signal_handler(int signo)
{
    aborted = signo;
}

static char *
get_config_path(void)
{
    struct passwd *passwd = getpwuid(getuid());
    if (passwd == NULL) {
        LOG_ERRNO("failed to lookup user");
        return NULL;
    }

    const char *home_dir = passwd->pw_dir;
    LOG_DBG("user's home directory: %s", home_dir);

    long path_max = sysconf(_PC_PATH_MAX);
    if (path_max == -1)
        path_max = 1024;

    char *path = malloc(path_max + 1);
    snprintf(path, path_max + 1, "%s/.config/f00bar/config.yml", home_dir);
    return path;
}

static FILE *
open_config(const char *path)
{
    FILE *ret = fopen(path, "r");
    if (ret == NULL)
        LOG_ERRNO("%s: failed to open", path);

    return ret;
}

int
main(int argc, const char *const *argv)
{
    char *config_path = get_config_path();
    FILE *conf_file = open_config(config_path);

    if (conf_file == NULL) {
        free(config_path);
        return 1;
    }

    char *yml_error;
    struct yml_node *conf = yml_load(conf_file, &yml_error);
    fclose(conf_file);

    if (conf == NULL) {
        LOG_ERR("%s:%s", config_path, yml_error);
        free(yml_error);
        free(config_path);
        return 1;
    }

    const struct yml_node *bar_conf = yml_get_value(conf, "bar");
    if (bar_conf == NULL) {
        LOG_ERR("%s: missing required top level key 'bar'", config_path);
        free(config_path);
        return 1;
    }

    xcb_init();

    const struct sigaction sa = {.sa_handler = &signal_handler};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int abort_fd = eventfd(0, EFD_CLOEXEC);
    if (abort_fd == -1) {
        LOG_ERRNO("failed to create eventfd (for abort signalling)");
        free(config_path);
        return 1;
    }

    struct bar *bar = conf_to_bar(bar_conf);
    if (bar == NULL) {
        LOG_ERR("%s: failed to load configuration", config_path);
        free(config_path);
        return 1;
    }

    free(config_path);

    struct bar_run_context bar_ctx = {
        .bar = bar,
        .abort_fd = abort_fd,
    };

    /* Block SIGINT (this is under the assumption that threads inherit
     * the signal mask */
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    thrd_t bar_thread;
    thrd_create(&bar_thread, (int (*)(void *))bar->run, &bar_ctx);

    /* Now unblock. We should be only thread receiving SIGINT */
    pthread_sigmask(SIG_UNBLOCK, &signal_mask, NULL);

    /* Connect to XCB, to be able to detect a disconnect (allowing us
     * to exit) */
    xcb_connection_t *xcb = xcb_connect(NULL, NULL);
    assert(xcb != NULL);

    /* Wait for SIGINT, or XCB disconnect */
    while (!aborted) {
        struct pollfd fds[] = {
            {.fd = xcb_get_file_descriptor(xcb), .events = POLLPRI}
        };

        poll(fds, 1, -1);

        if (aborted)
            break;

        LOG_INFO("XCB poll data");

        if (fds[0].revents & POLLHUP) {
            LOG_INFO("disconnected from XCB, exiting");
            break;
        }
    }

    xcb_disconnect(xcb);

    if (aborted)
        LOG_INFO("aborted: %s (%d)", strsignal(aborted), aborted);

    /* Signal abort to other threads */
    write(abort_fd, &(uint64_t){1}, sizeof(uint64_t));

    int res;
    int r = thrd_join(bar_thread, &res);
    if (r != 0)
        LOG_ERRNO_P("failed to join bar thread", r);

    bar->destroy(bar);
    yml_destroy(conf);

    close(abort_fd);
    return res;
}
