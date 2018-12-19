#include "xwindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <threads.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#include "../../bar.h"
#include "../../xcb.h"

struct private {
    /* Accessed from bar thread only */
    struct particle *label;

    /* Accessed from both our thread, and the bar thread */
    mtx_t lock;
    char *application;
    char *title;

    /* Accessed from our thread only */
    xcb_connection_t *conn;
    xcb_window_t root_win;
    xcb_window_t monitor_win;
    xcb_window_t active_win;
};

static void
update_active_window(struct private *m)
{
    if (m->active_win != 0) {
        xcb_change_window_attributes(
            m->conn, m->active_win, XCB_CW_EVENT_MASK,
            (const uint32_t []){XCB_EVENT_MASK_NO_EVENT});

        m->active_win = 0;
    }

    xcb_get_property_cookie_t c = xcb_get_property(
        m->conn, 0, m->root_win, _NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 0, 32);

    xcb_generic_error_t *e;
    xcb_get_property_reply_t *r = xcb_get_property_reply(m->conn, c, &e);

    if (e != NULL) {
        free(e);
        free(r);
        return;
    }

    assert(sizeof(m->active_win) == xcb_get_property_value_length(r));
    memcpy(&m->active_win, xcb_get_property_value(r), sizeof(m->active_win));
    free(r);

    if (m->active_win != 0) {
        xcb_change_window_attributes(
            m->conn, m->active_win, XCB_CW_EVENT_MASK,
            (const uint32_t []){XCB_EVENT_MASK_PROPERTY_CHANGE});
    }
}

static void
update_application(struct private *m)
{
    mtx_lock(&m->lock);
    free(m->application);
    m->application = NULL;
    mtx_unlock(&m->lock);

    if (m->active_win == 0)
        return;

    xcb_get_property_cookie_t c = xcb_get_property(
        m->conn, 0, m->active_win, _NET_WM_PID, XCB_ATOM_CARDINAL, 0, 32);

    xcb_generic_error_t *e;
    xcb_get_property_reply_t *r = xcb_get_property_reply(m->conn, c, &e);

    if (e != NULL) {
        free(e);
        free(r);
        return;
    }

    uint32_t pid;
    assert(xcb_get_property_value_length(r) == sizeof(pid));

    memcpy(&pid, xcb_get_property_value(r), sizeof(pid));
    free(r);

    char path[1024];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return;

    char cmd[1024] = {0};
    ssize_t bytes = read(fd, cmd, sizeof(cmd) - 1);
    close(fd);

    if (bytes == -1)
        return;

    mtx_lock(&m->lock);
    m->application = strdup(basename(cmd));
    mtx_unlock(&m->lock);
}

static void
update_title(struct private *m)
{
    mtx_lock(&m->lock);
    free(m->title);
    m->title = NULL;
    mtx_unlock(&m->lock);

    if (m->active_win == 0)
        return;

    xcb_get_property_cookie_t c1 = xcb_get_property(
        m->conn, 0, m->active_win, _NET_WM_VISIBLE_NAME, UTF8_STRING, 0, 1000);
    xcb_get_property_cookie_t c2 = xcb_get_property(
        m->conn, 0, m->active_win, _NET_WM_NAME, UTF8_STRING, 0, 1000);
    xcb_get_property_cookie_t c3 = xcb_get_property(
        m->conn, 0, m->active_win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 1000);

    xcb_generic_error_t *e1, *e2, *e3;
    xcb_get_property_reply_t *r1 = xcb_get_property_reply(m->conn, c1, &e1);
    xcb_get_property_reply_t *r2 = xcb_get_property_reply(m->conn, c2, &e2);
    xcb_get_property_reply_t *r3 = xcb_get_property_reply(m->conn, c3, &e3);

    const char *title;
    int title_len;

    if (e1 == NULL && xcb_get_property_value_length(r1) > 0) {
        title = xcb_get_property_value(r1);
        title_len = xcb_get_property_value_length(r1);
    } else if (e2 == NULL && xcb_get_property_value_length(r2) > 0) {
        title = xcb_get_property_value(r2);
        title_len = xcb_get_property_value_length(r2);
    } else if (e3 == NULL && xcb_get_property_value_length(r3) > 0) {
        title = xcb_get_property_value(r3);
        title_len = xcb_get_property_value_length(r3);
    } else {
        title = NULL;
        title_len = 0;
    }

    if (title_len > 0) {
        mtx_lock(&m->lock);
        m->title = malloc(title_len + 1);
        memcpy(m->title, title, title_len);
        m->title[title_len] = '\0';
        mtx_unlock(&m->lock);
    }

    free(e1);
    free(e2);
    free(e3);
    free(r1);
    free(r2);
    free(r3);
 }

static int
run(struct module_run_context *ctx)
{
    struct module *mod = ctx->module;
    struct private *m = mod->private;

    m->conn = xcb_connect(NULL, NULL);
    assert(m->conn != NULL);

    const xcb_setup_t *setup = xcb_get_setup(m->conn);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    m->root_win = screen->root;

    /* Need a window(?) to be able to process events */
    m->monitor_win = xcb_generate_id(m->conn);
    xcb_create_window(m->conn, screen->root_depth, m->monitor_win, screen->root,
                      -1, -1, 1, 1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT, (const uint32_t []){1});

    xcb_map_window(m->conn, m->monitor_win);

    /* Register for property changes on root window. This allows us to
     * catch e.g. window switches etc */
    xcb_change_window_attributes(
        m->conn, screen->root, XCB_CW_EVENT_MASK,
        (const uint32_t []){XCB_EVENT_MASK_PROPERTY_CHANGE});

    xcb_flush(m->conn);

    update_active_window(m);
    update_application(m);
    update_title(m);
    mod->bar->refresh(mod->bar);

    int xcb_fd = xcb_get_file_descriptor(m->conn);
    while (true) {
        struct pollfd fds[] = {{.fd = ctx->abort_fd, .events = POLLIN},
                               {.fd = xcb_fd, .events = POLLIN}};
        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN)
            break;

        for (xcb_generic_event_t *_e = xcb_wait_for_event(m->conn);
             _e != NULL;
             _e = xcb_poll_for_event(m->conn))
        {
            switch (XCB_EVENT_RESPONSE_TYPE(_e)) {
            case XCB_PROPERTY_NOTIFY: {
                xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)_e;
                if (e->atom == _NET_ACTIVE_WINDOW ||
                    e->atom == _NET_CURRENT_DESKTOP)
                {
                    /* Active desktop and/or window changed */
                    update_active_window(m);
                    update_application(m);
                    update_title(m);
                    mod->bar->refresh(mod->bar);
                } else if (e->atom == _NET_WM_VISIBLE_NAME ||
                           e->atom == _NET_WM_NAME ||
                           e->atom == XCB_ATOM_WM_NAME)
                {
                    assert(e->window == m->active_win);
                    update_title(m);
                    mod->bar->refresh(mod->bar);
                }
                break;
            }

            case 0: break;  /* error */
            }

            free(_e);
        }
    }

    xcb_destroy_window(m->conn, m->monitor_win);
    xcb_disconnect(m->conn);
    return 0;
}

static struct exposable *
content(const struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&m->lock);
    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string("application", m->application ? m->application : ""),
            tag_new_string("title", m->title ? m->title : "")},
        .count = 2,
    };
    mtx_unlock(&m->lock);

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    mtx_destroy(&m->lock);
    free(m->application);
    free(m->title);
    free(m);
    module_default_destroy(mod);
}

struct module *
module_xwindow(struct particle *label)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;
    mtx_init(&m->lock, mtx_plain);

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
