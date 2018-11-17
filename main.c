#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include <threads.h>

#include <sys/eventfd.h>

#include "bar.h"
#include "config.h"
#include "yml.h"
#include "xcb.h"

static volatile sig_atomic_t aborted = 0;

static void
signal_handler(int signo)
{
    aborted = 1;
}

int
main(int argc, const char *const *argv)
{
    FILE *conf_file = fopen("config.yml", "r");
    assert(conf_file != NULL);
    struct yml_node *conf = yml_load(conf_file);
    fclose(conf_file);

    xcb_init();

    const struct sigaction sa = {.sa_handler = &signal_handler};
    sigaction(SIGINT, &sa, NULL);

    int abort_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    assert(abort_fd >= 0);

    struct bar *bar = conf_to_bar(yml_get_value(conf, "bar"));

#if 1
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
#endif
    bar->destroy(bar);
    yml_destroy(conf);

    close(abort_fd);
    return 0;
}
