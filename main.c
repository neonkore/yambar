#include <assert.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/eventfd.h>
#include <pwd.h>

#include "bar.h"
#include "config.h"
#include "yml.h"

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
    snprintf(path, path_max + 1, "%s/.config/f00bar/config-wayland.yml", home_dir);
    return path;
}

static struct bar *
load_bar(const char *config_path)
{
    FILE *conf_file = fopen(config_path, "r");
    if (conf_file == NULL) {
        LOG_ERRNO("%s: failed to open", config_path);
        return NULL;
    }

    struct bar *bar = NULL;
    char *yml_error = NULL;

    struct yml_node *conf = yml_load(conf_file, &yml_error);
    if (conf == NULL) {
        LOG_ERR("%s:%s", config_path, yml_error);
        goto out;
    }

    const struct yml_node *bar_conf = yml_get_value(conf, "bar");
    if (bar_conf == NULL) {
        LOG_ERR("%s: missing required top level key 'bar'", config_path);
        goto out;
    }

    bar = conf_to_bar(bar_conf);
    if (bar == NULL) {
        LOG_ERR("%s: failed to load configuration", config_path);
        goto out;
    }

out:
    free(yml_error);
    yml_destroy(conf);
    fclose(conf_file);
    return bar;
}

int
main(int argc, const char *const *argv)
{
    setlocale(LC_ALL, "");

    const struct sigaction sa = {.sa_handler = &signal_handler};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Block SIGINT (this is under the assumption that threads inherit
     * the signal mask */
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    int abort_fd = eventfd(0, EFD_CLOEXEC);
    if (abort_fd == -1) {
        LOG_ERRNO("failed to create eventfd (for abort signalling)");
        return 1;
    }

    char *config_path = get_config_path();

    struct bar *bar = load_bar(config_path);
    free(config_path);

    if (bar == NULL) {
        close(abort_fd);
        return 1;
    }

    bar->abort_fd = abort_fd;

    thrd_t bar_thread;
    thrd_create(&bar_thread, (int (*)(void *))bar->run, bar);

    /* Now unblock. We should be only thread receiving SIGINT */
    pthread_sigmask(SIG_UNBLOCK, &signal_mask, NULL);

    while (!aborted) {
        struct pollfd fds[] = {{.fd = abort_fd, .events = POLLIN}};
        int r __attribute__((unused)) = poll(fds, 1, -1);

        /*
         * Either the bar aborted (triggering the abort_fd), or user
         * killed us (triggering the signal handler which sets
         * 'aborted')
         */
        assert(aborted || r == 1);
        break;
    }

    if (aborted)
        LOG_INFO("aborted: %s (%d)", strsignal(aborted), aborted);

    /* Signal abort to other threads */
    write(abort_fd, &(uint64_t){1}, sizeof(uint64_t));

    int res;
    int r = thrd_join(bar_thread, &res);
    if (r != 0)
        LOG_ERRNO_P("failed to join bar thread", r);

    bar->destroy(bar);
    close(abort_fd);
    return res;
}
