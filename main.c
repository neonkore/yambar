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

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/render.h>

#include "bar.h"
#include "config.h"
#include "yml.h"


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

    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    assert(conn != NULL);

    const xcb_setup_t *setup = xcb_get_setup(conn);

    /* Vendor string */
    int length = xcb_setup_vendor_length(setup);
    char *vendor = malloc(length + 1);
    memcpy(vendor, xcb_setup_vendor(setup), length);
    vendor[length] = '\0';

    /* Vendor release number */
    unsigned release = setup->release_number;
    unsigned major = release / 10000000; release %= 10000000;
    unsigned minor = release / 100000; release %= 100000;
    unsigned patch = release / 1000;

    printf("%s %u.%u.%u (protocol: %u.%u)\n", vendor,
           major, minor, patch,
           setup->protocol_major_version,
           setup->protocol_minor_version);
    free(vendor);

    const xcb_query_extension_reply_t *randr =
        xcb_get_extension_data(conn, &xcb_randr_id);
    assert(randr->present);

    const xcb_query_extension_reply_t *render =
        xcb_get_extension_data(conn, &xcb_render_id);
    assert(render->present);

    xcb_randr_query_version_cookie_t randr_cookie =
        xcb_randr_query_version(conn, XCB_RANDR_MAJOR_VERSION,
                                XCB_RANDR_MINOR_VERSION);
    xcb_render_query_version_cookie_t render_cookie =
        xcb_render_query_version(conn, XCB_RENDER_MAJOR_VERSION,
                                 XCB_RENDER_MINOR_VERSION);

    xcb_generic_error_t *e;
    xcb_randr_query_version_reply_t *randr_version =
        xcb_randr_query_version_reply(conn, randr_cookie, &e);
    assert(e == NULL);

    xcb_render_query_version_reply_t *render_version =
        xcb_render_query_version_reply(conn, render_cookie, &e);
    assert(e == NULL);

    xcb_flush(conn);

    printf("  RANDR: %u.%u\n"
           "  RENDER: %u.%u\n",
           randr_version->major_version,
           randr_version->minor_version,
           render_version->major_version,
           render_version->minor_version);

    free(randr_version);
    free(render_version);

    xcb_disconnect(conn);

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
