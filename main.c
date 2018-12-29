#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include <threads.h>

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
    aborted = 1;
}

static FILE *
open_config(void)
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

    char path[path_max];
    snprintf(path, path_max, "%s/.config/f00bar/config.yml", home_dir);

    FILE *ret = fopen(path, "r");
    if (ret == NULL)
        LOG_ERRNO("%s: failed to open", path);

    return ret;
}

int
main(int argc, const char *const *argv)
{
    FILE *conf_file = open_config();
    if (conf_file == NULL)
        return 1;

    struct yml_node *conf = yml_load(conf_file);
    fclose(conf_file);

    xcb_init();

    const struct sigaction sa = {.sa_handler = &signal_handler};
    sigaction(SIGINT, &sa, NULL);

    int abort_fd = eventfd(0, EFD_CLOEXEC);
    assert(abort_fd >= 0);

    struct bar *bar = conf_to_bar(yml_get_value(conf, "bar"));

    struct bar_run_context bar_ctx = {
        .bar = bar,
        .abort_fd = abort_fd,
    };

    thrd_t bar_thread;
    thrd_create(&bar_thread, (int (*)(void *))bar->run, &bar_ctx);

    while (!aborted) {
        sleep(999999999);
    }

    /* Signal abort to all workers */
    write(abort_fd, &(uint64_t){1}, sizeof(uint64_t));

    int res;
    thrd_join(bar_thread, &res);

    bar->destroy(bar);
    yml_destroy(conf);

    close(abort_fd);
    return 0;
}
