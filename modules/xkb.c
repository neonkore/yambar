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
#include "../config.h"
#include "../config-verify.h"
#include "../xcb.h"

struct layout {
    char *name;
    char *symbol;
};

struct layouts {
    size_t count;
    struct layout *layouts;
};

struct indicators {
    size_t count;
    char **names;
};

struct private {
    struct particle *label;
    struct indicators indicators;
    struct layouts layouts;
    size_t current;

    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
};

static void
free_layouts(struct layouts layouts)
{
    for (size_t i = 0; i < layouts.count; i++) {
        free(layouts.layouts[i].name);
        free(layouts.layouts[i].symbol);
    }
    free(layouts.layouts);
}

static void
free_indicators(struct indicators indicators)
{
    for (size_t i = 0; i < indicators.count; i++)
        free(indicators.names[i]);
    free(indicators.names);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free_layouts(m->layouts);
    free_indicators(m->indicators);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const char *name = "";
    const char *symbol = "";

    if (m->current < m->layouts.count) {
        name = m->layouts.layouts[m->current].name;
        symbol = m->layouts.layouts[m->current].symbol;
    }

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", name),
            tag_new_string(mod, "symbol", symbol),
            tag_new_bool(mod, "caps_lock", m->caps_lock),
            tag_new_bool(mod, "num_lock", m->num_lock),
            tag_new_bool(mod, "scroll_lock", m->scroll_lock),
        },
        .count = 5,
    };

    mtx_unlock(&mod->lock);

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

    if (err != NULL) {
        LOG_ERR("failed to query for XKB extension: %s", xcb_error(err));
        free(err);
        free(reply);
        return false;
    }

    if (!reply->supported) {
        LOG_ERR("XKB extension is not supported");
        free(reply);
        return false;
    }

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

static bool
get_layouts_and_indicators(xcb_connection_t *conn, struct layouts *layouts,
                           struct indicators *indicators)
{
    xcb_generic_error_t *err;
    xcb_xkb_get_names_cookie_t cookie = xcb_xkb_get_names(
        conn,
        XCB_XKB_ID_USE_CORE_KBD,
        XCB_XKB_NAME_DETAIL_GROUP_NAMES |
        XCB_XKB_NAME_DETAIL_SYMBOLS |
        XCB_XKB_NAME_DETAIL_INDICATOR_NAMES);

    xcb_xkb_get_names_reply_t *reply = xcb_xkb_get_names_reply(
        conn, cookie, &err);

    if (err != NULL) {
        LOG_ERR("failed to get layouts and indicators: %s", xcb_error(err));
        free(err);
        return false;
    }

    xcb_xkb_get_names_value_list_t vlist;
    void *buf = xcb_xkb_get_names_value_list(reply);
    xcb_xkb_get_names_value_list_unpack(
        buf, reply->nTypes, reply->indicators, reply->virtualMods,
        reply->groupNames, reply->nKeys, reply->nKeyAliases,
        reply->nRadioGroups, reply->which, &vlist);

    /* Number of groups (aka layouts) */
    layouts->count = xcb_xkb_get_names_value_list_groups_length(reply, &vlist);
    layouts->layouts = calloc(layouts->count, sizeof(layouts->layouts[0]));

    /* Number of indicators */
    indicators->count = xcb_xkb_get_names_value_list_indicator_names_length(
        reply, &vlist);
    indicators->names = calloc(indicators->count, sizeof(indicators->names[0]));

    xcb_get_atom_name_cookie_t symbols_name_cookie = xcb_get_atom_name(
        conn, vlist.symbolsName);

    xcb_get_atom_name_cookie_t group_name_cookies[layouts->count];
    for (size_t i = 0; i < layouts->count; i++)
        group_name_cookies[i] = xcb_get_atom_name(conn, vlist.groups[i]);

    xcb_get_atom_name_cookie_t indicator_cookies[indicators->count];
    for (size_t i = 0; i < indicators->count; i++)
        indicator_cookies[i] = xcb_get_atom_name(conn, vlist.indicatorNames[i]);

    char *symbols = NULL;

    /* Get layout short names (e.g. "us") */
    xcb_get_atom_name_reply_t *atom_name = xcb_get_atom_name_reply(
        conn, symbols_name_cookie, &err);
    if (err != NULL) {
        LOG_ERR("failed to get 'symbols' atom name: %s", xcb_error(err));
        free(err);
        goto err;
    }

    symbols = strndup(
        xcb_get_atom_name_name(atom_name),
        xcb_get_atom_name_name_length(atom_name));
    LOG_DBG("symbols: %s", symbols);
    free(atom_name);

    /* Get layout long names (e.g. "English (US)") */
    for (size_t i = 0; i < layouts->count; i++) {
        atom_name = xcb_get_atom_name_reply(conn, group_name_cookies[i], &err);
        if (err != NULL) {
            LOG_ERR("failed to get 'group' atom name: %s", xcb_error(err));
            free(err);
            goto err;
        }

        layouts->layouts[i].name = strndup(
            xcb_get_atom_name_name(atom_name),
            xcb_get_atom_name_name_length(atom_name));

        LOG_DBG("layout #%zd: long name: %s", i, layouts->layouts[i].name);
        free(atom_name);
    }

    /* Indicator names e.g. "Caps Lock", "Num Lock" */
    for (size_t i = 0; i < indicators->count; i++) {
        atom_name = xcb_get_atom_name_reply(conn, indicator_cookies[i], &err);
        if (err != NULL) {
            LOG_ERR("failed to get 'indicator' atom name: %s", xcb_error(err));
            free(err);
            goto err;
        }

        indicators->names[i] = strndup(
            xcb_get_atom_name_name(atom_name),
            xcb_get_atom_name_name_length(atom_name));

        LOG_DBG("indicator #%zd: %s", i, indicators->names[i]);
        free(atom_name);
    }

    /* e.g. pc+us+inet(evdev)+group(..) */
    size_t layout_idx = 0;
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

        if (layout_idx >= layouts->count) {
            LOG_ERR("layout vs group name count mismatch: %zd > %zd",
                    layout_idx + 1, layouts->count);
            goto err;
        }

        char *sym = strdup(fname);
        layouts->layouts[layout_idx++].symbol = sym;
        LOG_DBG("layout #%zd: short name: %s", layout_idx - 1, sym);
    }

    if (layout_idx != layouts->count) {
        LOG_ERR("layout vs group name count mismatch: %zd != %zd",
                layout_idx, layouts->count);
        goto err;
    }

    free(symbols);
    free(reply);
    return true;

err:
    free(symbols);
    free(reply);
    free_layouts(*layouts);
    free_indicators(*indicators);
    return false;
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
        LOG_ERR("failed to get XKB state: %s", xcb_error(err));
        return -1;
    }

    int ret = reply->group;

    free(reply);
    return ret;
}

static uint32_t
get_indicator_state(xcb_connection_t *conn)
{
    xcb_generic_error_t *err;
    xcb_xkb_get_indicator_state_cookie_t cookie = xcb_xkb_get_indicator_state(
        conn, XCB_XKB_ID_USE_CORE_KBD);
    xcb_xkb_get_indicator_state_reply_t *reply = xcb_xkb_get_indicator_state_reply(
        conn, cookie, &err);

    if (err != NULL) {
        LOG_ERR("failed to get indicator state: %s", xcb_error(err));
        free(err);
        return (uint32_t)-1;
    }

    uint32_t state = reply->state;
    LOG_DBG("indicator state: 0x%08x", state);

    free(reply);
    return state;
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
        LOG_ERR("failed to register for events: %s", xcb_error(err));
        return false;
    }

    return true;
}

static bool
event_loop(struct module *mod, xcb_connection_t *conn, int xkb_event_base)
{
    const struct bar *bar = mod->bar;
    struct private *m = mod->private;

    bool ret = false;
    bool has_error = false;

    const int xcb_fd = xcb_get_file_descriptor(conn);
    assert(xcb_fd >= 0);

    while (!has_error) {
        struct pollfd pfds[] = {
            {.fd = mod->abort_fd, .events = POLLIN },
            {.fd = xcb_fd, .events = POLLIN | POLLHUP }
        };

        /* Use poll() since xcb_wait_for_events() doesn't return on signals */
        poll(pfds, sizeof(pfds) / sizeof(pfds[0]), -1);
        if (pfds[0].revents & POLLIN) {
            ret = true;
            break;
        }

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

                struct layouts layouts;
                struct indicators indicators;
                if (!get_layouts_and_indicators(conn, &layouts, &indicators)) {
                    has_error = true;
                    break;
                }

                if (current < layouts.count) {
                    mtx_lock(&mod->lock);
                    free_layouts(m->layouts);
                    m->current = current;
                    m->layouts = layouts;
                    m->indicators = indicators;
                    mtx_unlock(&mod->lock);
                    bar->refresh(bar);
                } else {
                     /* Can happen while transitioning to a new map */
                    free_layouts(layouts);
                    free_indicators(indicators);
                }

                break;
            }

            case XCB_XKB_STATE_NOTIFY: {
                const xcb_xkb_state_notify_event_t *evt =
                    (const xcb_xkb_state_notify_event_t *)_evt;

                if (evt->changed & XCB_XKB_STATE_PART_GROUP_STATE) {
                    mtx_lock(&mod->lock);
                    m->current = evt->group;
                    mtx_unlock(&mod->lock);
                    bar->refresh(bar);
                }

                break;
            }

            case XCB_XKB_MAP_NOTIFY:
                LOG_WARN("map event unimplemented");
                break;

            case XCB_XKB_INDICATOR_STATE_NOTIFY: {
                const xcb_xkb_indicator_state_notify_event_t *evt =
                    (const xcb_xkb_indicator_state_notify_event_t *)_evt;

#if 0
                size_t idx = __builtin_ctz(evt->stateChanged);
                LOG_ERR("%zu", idx);
                if (idx < m->indicators.count)
                    LOG_ERR("%s", m->indicators.names[idx]);
#endif

                /* TODO: bit count evt->stateChanged instead */
                bool need_refresh = false;
                for (size_t i = 0; i < m->indicators.count; i++) {
                    bool changed = (evt->stateChanged >> i) & 1;
                    if (!changed)
                        continue;

                    bool enabled = (evt->state >> i) & 1;
                    LOG_DBG("%s: %s", m->indicators.names[i],
                            enabled ? "enabled" : "disabled");

                    const char *name = m->indicators.names[i];
                    bool is_caps = strcasecmp(name, "caps lock") == 0;
                    bool is_num = strcasecmp(name, "num lock") == 0;

                    if (is_caps || is_num) {
                        mtx_lock(&mod->lock);

                        if (is_caps)
                            m->caps_lock = enabled;
                        else if (is_num)
                            m->num_lock = enabled;

                        mtx_unlock(&mod->lock);
                        need_refresh = true;
                    }
                }

                if (need_refresh)
                    bar->refresh(bar);
                break;
            }
            }

            free(_evt);
        }
    }

    return ret;
}

static bool
talk_to_xkb(struct module *mod, xcb_connection_t *conn)
{
    struct private *m = mod->private;

    if (!xkb_enable(conn))
        return false;

    if (!register_for_events(conn))
        return false;

    int xkb_event_base = get_xkb_event_base(conn);
    if (xkb_event_base == -1)
        return false;

    int current = get_current_layout(conn);
    if (current == -1)
        return false;

    /* Bitmask, one bit for every indicator available */
    uint32_t indicator_state = get_indicator_state(conn);
    if (indicator_state == (uint32_t)-1)
        return false;

    struct layouts layouts;
    struct indicators indicators;
    if (!get_layouts_and_indicators(conn, &layouts, &indicators))
        return false;

    if (current >= layouts.count) {
        LOG_ERR("current layout index: %d >= %zd", current, layouts.count);
        free_layouts(layouts);
        free_indicators(indicators);
        return false;
    }

    bool caps_lock = false, num_lock = false, scroll_lock = false;

    /* Lookup initial state of caps-, num- and scroll lock */
    for (size_t i = 0; i < indicators.count; i++) {
        const char *name = indicators.names[i];
        bool enabled = (indicator_state >> i) & 1;

        bool is_caps = strcasecmp(name, "caps lock") == 0;
        bool is_num = strcasecmp(name, "num lock") == 0;
        bool is_scroll = strcasecmp(name, "scroll lock") == 0;

        if (!(is_caps || is_num || is_scroll))
            continue;

        if (is_caps)
            caps_lock = enabled;
        else if (is_num)
            num_lock = enabled;
        else if (is_scroll)
            scroll_lock = enabled;

        LOG_DBG("%s: %s", name, enabled ? "enabled" : "disabled");
    }

    mtx_lock(&mod->lock);
    m->layouts = layouts;
    m->current = current;
    m->indicators = indicators;
    m->caps_lock = caps_lock;
    m->num_lock = num_lock;
    m->scroll_lock = scroll_lock;
    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);

    return event_loop(mod, conn, xkb_event_base);
}

static int
run(struct module *mod)
{
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (conn == NULL) {
        LOG_ERR("failed to connect to X server");
        return EXIT_FAILURE;
    }

    int ret = talk_to_xkb(mod, conn) ? EXIT_SUCCESS : EXIT_FAILURE;

    xcb_disconnect(conn);
    return ret;
}

static struct module *
xkb_new(struct particle *label)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;
    m->current = 0;
    m->layouts.count = 0;
    m->layouts.layouts = NULL;
    m->caps_lock = false;
    m->num_lock = false;
    m->scroll_lock = false;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

struct module *
xkb_from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    return xkb_new(conf_to_particle(c, inherited));
}

bool
xkb_verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

bool verify_conf(keychain_t *chain, const struct yml_node *node)
    __attribute__((weak, alias("xkb_verify_conf")));
struct deco *from_conf(const struct yml_node *node, struct conf_inherit inherited)
    __attribute__((weak, alias("xkb_from_conf")));

#endif
