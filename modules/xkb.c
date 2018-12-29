#include "xkb.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <poll.h>

#include <xcb/xcb.h>
#include <xcb/xkb.h>

#define LOG_MODULE "xkb"
#include "../log.h"
#include "../bar.h"
#include "../xcb.h"

struct layout {
    char *name;
    char *symbol;
};

struct layouts {
    ssize_t count;
    struct layout *layouts;
};

struct private {
    struct particle *label;
    struct layouts layouts;
    size_t current;
};

static void
free_layouts(struct layouts layouts)
{
    for (ssize_t i = 0; i < layouts.count; i++) {
        free(layouts.layouts[i].name);
        free(layouts.layouts[i].symbol);
    }
    free(layouts.layouts);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free_layouts(m->layouts);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", m->layouts.layouts[m->current].name),
            tag_new_string(mod, "symbol", m->layouts.layouts[m->current].symbol)},
        .count = 2,
    };

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static bool
xkb_enable(xcb_connection_t *conn)
{
    xcb_generic_error_t *err;

    xcb_xkb_use_extension_cookie_t cookie = xcb_xkb_use_extension(
        conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
    xcb_xkb_use_extension_reply_t *reply = xcb_xkb_use_extension_reply(
        conn, cookie, &err);

    assert(err == NULL && "xcb_xkb_use_extension() failed");
    assert(reply->supported && "XKB extension not supported");
    free(reply);
    return true;
}

static int
get_xkb_event_base(xcb_connection_t *conn)
{
    const struct xcb_query_extension_reply_t *reply = xcb_get_extension_data(
        conn, &xcb_xkb_id);

    if (reply == NULL) {
        LOG_ERR("failed to get XKB extension data");
        return -1;
    }

    if (!reply->present) {
        LOG_ERR("XKB not present");
        return -1;
    }

    return reply->first_event;
}

static struct layouts
get_layouts(xcb_connection_t *conn)
{
    struct layouts ret;
    xcb_generic_error_t *err;

    xcb_xkb_get_names_cookie_t cookie = xcb_xkb_get_names(
        conn,
        XCB_XKB_ID_USE_CORE_KBD,
        XCB_XKB_NAME_DETAIL_GROUP_NAMES |
        XCB_XKB_NAME_DETAIL_SYMBOLS);

    xcb_xkb_get_names_reply_t *reply = xcb_xkb_get_names_reply(
        conn, cookie, &err);

    if (err != NULL) {
        LOG_ERR("failed to get group names and symbols (%d)", err->error_code);
        free(err);
        return (struct layouts){.count = -1};
    }

    xcb_xkb_get_names_value_list_t vlist;
    void *buf = xcb_xkb_get_names_value_list(reply);
    xcb_xkb_get_names_value_list_unpack(
        buf, reply->nTypes, reply->indicators, reply->virtualMods,
        reply->groupNames, reply->nKeys, reply->nKeyAliases,
        reply->nRadioGroups, reply->which, &vlist);

    /* Number of groups (aka layouts) */
    ret.count = xcb_xkb_get_names_value_list_groups_length(reply, &vlist);
    ret.layouts = calloc(ret.count, sizeof(ret.layouts[0]));

    xcb_get_atom_name_cookie_t symbols_name_cookie = xcb_get_atom_name(
        conn, vlist.symbolsName);

    xcb_get_atom_name_cookie_t group_name_cookies[ret.count];
    for (ssize_t i = 0; i < ret.count; i++)
        group_name_cookies[i] = xcb_get_atom_name(conn, vlist.groups[i]);

    /* Get layout short names (e.g. "us") */
    xcb_get_atom_name_reply_t *atom_name = xcb_get_atom_name_reply(
        conn, symbols_name_cookie, &err);
    if (err != NULL) {
        LOG_ERR("failed to get atom name (%d)", err->error_code);
        free(err);
        goto err;
    }

    char *symbols = strndup(xcb_get_atom_name_name(atom_name),
                            xcb_get_atom_name_name_length(atom_name));
    free(atom_name);

    /* Get layout long names (e.g. "English (US)") */
    for (ssize_t i = 0; i < ret.count; i++) {
        atom_name = xcb_get_atom_name_reply(conn, group_name_cookies[i], &err);
        if (err != NULL) {
            LOG_ERR("failed to get atom name (%d)", err->error_code);
            free(err);
            goto err;
        }

        ret.layouts[i].name = strndup(xcb_get_atom_name_name(atom_name),
                                      xcb_get_atom_name_name_length(atom_name));
        free(atom_name);
    }

    /* e.g. pc+us+inet(evdev)+group(..) */
    ssize_t layout_idx = 0;
    for (char *tok_ctx = NULL, *tok = strtok_r(symbols, "+", &tok_ctx);
         tok != NULL;
         tok = strtok_r(NULL, "+", &tok_ctx)) {

        char *fname = strtok(tok, "()");
        char *section __attribute__((unused)) = strtok(NULL, "()");

        /* Not sure why some layouts have a ":n" suffix (where
         * 'n' is a number, e.g. "us:2") */
        fname = strtok(fname, ":");

        /* Assume all language layouts are two-letters */
        if (strlen(fname) != 2)
            continue;

        /* But make sure to ignore "pc" :) */
        if (strcmp(tok, "pc") == 0)
            continue;

        if (layout_idx >= ret.count) {
            LOG_ERR("layout vs group name count mismatch: %zd > %zd",
                    layout_idx + 1, ret.count);
            goto err;
        }

        char *sym = strdup(fname);
        ret.layouts[layout_idx++].symbol = sym;
    }

    if (layout_idx != ret.count) {
        LOG_ERR("layout vs group name count mismatch: %zd != %zd",
                layout_idx, ret.count);
        goto err;
    }

    free(symbols);
    free(reply);

    return ret;

err:
    free(symbols);
    free(reply);
    free_layouts(ret);
    return (struct layouts){.count = -1};
}

static int
get_current_layout(xcb_connection_t *conn)
{
    xcb_generic_error_t *err;

    xcb_xkb_get_state_cookie_t cookie = xcb_xkb_get_state(
        conn, XCB_XKB_ID_USE_CORE_KBD);
    xcb_xkb_get_state_reply_t *reply = xcb_xkb_get_state_reply(
        conn, cookie, &err);

    if (err != NULL) {
        LOG_ERR("failed to get XKB state (%d)", err->error_code);
        return -1;
    }

    int ret = reply->group;

    free(reply);
    return ret;
}

static bool
register_for_events(xcb_connection_t *conn)
{
    xcb_void_cookie_t cookie = xcb_xkb_select_events_checked(
        conn,
        XCB_XKB_ID_USE_CORE_KBD,
        (
            XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
            XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
            XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
            XCB_XKB_EVENT_TYPE_INDICATOR_STATE_NOTIFY
            ),
        0,
        (
            XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
            XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
            XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
            XCB_XKB_EVENT_TYPE_INDICATOR_STATE_NOTIFY
            ),
        0, 0, NULL);

    xcb_generic_error_t *err = xcb_request_check(conn, cookie);
    if (err != NULL) {
        LOG_ERR("failed to register for events (%d)", err->error_code);
        return false;
    }

    return true;
}

static bool
event_loop(struct module_run_context *ctx, xcb_connection_t *conn,
           int xkb_event_base)
{
    struct private *m = ctx->module->private;
    const struct bar *bar = ctx->module->bar;

    bool ret = false;
    bool has_error = false;

    const int xcb_fd = xcb_get_file_descriptor(conn);
    assert(xcb_fd >= 0);

    while (!has_error) {
        struct pollfd pfds[] = {
            {.fd = ctx->abort_fd, .events = POLLIN },
            {.fd = xcb_fd, .events = POLLIN | POLLHUP }
        };

        /* Use poll() since xcb_wait_for_events() doesn't return on signals */
        int pret = poll(pfds, sizeof(pfds) / sizeof(pfds[0]), -1);
        if (pfds[0].revents & POLLIN) {
            ret = true;
            break;
        }

        assert(pret == 1 && "unexpected poll(3) return value");

        if (pfds[1].revents & POLLHUP) {
            LOG_WARN("I/O error, server disconnect?");
            break;
        }

        assert(pfds[1].revents & POLLIN && "POLLIN not set");

        /*
         * Note: poll() might have returned *before* the entire event
         * has been received, and thus we might block here. Hopefully
         * not for long though...
         */

        for (xcb_generic_event_t *_evt = xcb_wait_for_event(conn);
             _evt != NULL;
             _evt = xcb_poll_for_event(conn)) {

            if (_evt->response_type != xkb_event_base) {
                LOG_WARN("non-XKB event ignored: %d", _evt->response_type);
                free(_evt);
                continue;
            }

            switch(_evt->pad0) {
            default:
                LOG_WARN("unimplemented XKB event: %d", _evt->pad0);
                break;

            case XCB_XKB_NEW_KEYBOARD_NOTIFY: {
                int current = get_current_layout(conn);
                if (current == -1) {
                    has_error = true;
                    break;
                }

                struct layouts layouts = get_layouts(conn);
                if (layouts.count == -1) {
                    has_error = true;
                    break;
                }

                if (current < layouts.count) {
                    free_layouts(m->layouts);
                    m->layouts = layouts;
                    m->current = current;
                    bar->refresh(bar);
                } else {
                     /* Can happen while transitioning to a new map */
                    free_layouts(layouts);
                }

                break;
            }

            case XCB_XKB_STATE_NOTIFY: {
                const xcb_xkb_state_notify_event_t *evt =
                    (const xcb_xkb_state_notify_event_t *)_evt;

                if (evt->changed & XCB_XKB_STATE_PART_GROUP_STATE) {
                    m->current = evt->group;
                    bar->refresh(bar);
                }

                break;
            }

            case XCB_XKB_MAP_NOTIFY:
                LOG_WARN("map event unimplemented");
                break;

            case XCB_XKB_INDICATOR_STATE_NOTIFY:
                LOG_WARN("indicator state event unimplemented");
                break;
            }

            free(_evt);
        }
    }

    return ret;
}

static bool
talk_to_xkb(struct module_run_context *ctx, xcb_connection_t *conn)
{
    struct private *m = ctx->module->private;

    if (!xkb_enable(conn))
        goto err;

    if (!register_for_events(conn))
        goto err;

    int xkb_event_base = get_xkb_event_base(conn);
    if (xkb_event_base == -1)
        goto err;

    int current = get_current_layout(conn);
    if (current == -1)
        goto err;

    struct layouts layouts = get_layouts(conn);
    if (layouts.count == -1)
        goto err;

    if (current >= layouts.count) {
        LOG_ERR("current layout index: %d >= %zd", current, layouts.count);
        free_layouts(layouts);
        goto err;
    }

    m->layouts = layouts;
    m->current = current;

    module_signal_ready(ctx);
    return event_loop(ctx, conn, xkb_event_base);

err:
    module_signal_ready(ctx);
    return false;
}

static int
run(struct module_run_context *ctx)
{
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (conn == NULL) {
        LOG_ERR("failed to connect to X server");
        module_signal_ready(ctx);
        return EXIT_FAILURE;
    }

    int ret = talk_to_xkb(ctx, conn) ? EXIT_SUCCESS : EXIT_FAILURE;

    xcb_disconnect(conn);
    return ret;
}

struct module *
module_xkb(struct particle *label)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;
    m->current = 0;
    m->layouts.count = 0;
    m->layouts.layouts = NULL;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
