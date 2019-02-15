#include "i3-common.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <poll.h>

#if defined(ENABLE_X11)
 #include <xcb/xcb.h>
 #include <xcb/xcb_aux.h>
#endif

#include <i3/ipc.h>
#include <json-c/json_tokener.h>

#define LOG_MODULE "i3:common"
#include "../log.h"

#if defined(ENABLE_X11)
 #include "../xcb.h"
#endif

#if defined(ENABLE_X11)
static bool
get_socket_address_x11(struct sockaddr_un *addr)
{
    int default_screen;
    xcb_connection_t *conn = xcb_connect(NULL, &default_screen);
    if (xcb_connection_has_error(conn) > 0) {
        LOG_ERR("failed to connect to X");
        xcb_disconnect(conn);
        return false;
    }

    xcb_screen_t *screen = xcb_aux_get_screen(conn, default_screen);

    xcb_atom_t atom = get_atom(conn, "I3_SOCKET_PATH");
    assert(atom != XCB_ATOM_NONE);

    xcb_get_property_cookie_t cookie
        = xcb_get_property_unchecked(
            conn, false, screen->root, atom,
            XCB_GET_PROPERTY_TYPE_ANY, 0, sizeof(addr->sun_path));

    xcb_generic_error_t *err = NULL;
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(conn, cookie, &err);
    bool ret = false;

    if (err != NULL) {
        LOG_ERR("failed to get i3 socket path: %s", xcb_error(err));
        goto err;
    }

    const int len = xcb_get_property_value_length(reply);
    assert(len < sizeof(addr->sun_path));

    if (len == 0) {
        LOG_ERR("failed to get i3 socket path: empty reply");
        goto err;
    }

    memcpy(addr->sun_path, xcb_get_property_value(reply), len);
    addr->sun_path[len] = '\0';

    ret = true;

err:
    free(err);
    free(reply);
    xcb_disconnect(conn);
    return ret;
}
#endif

bool
i3_get_socket_address(struct sockaddr_un *addr)
{
    *addr = (struct sockaddr_un){.sun_family = AF_UNIX};

    const char *sway_sock = getenv("SWAYSOCK");
    if (sway_sock == NULL) {
        sway_sock = getenv("I3SOCK");
        if (sway_sock == NULL) {
#if defined(ENABLE_X11)
            return get_socket_address_x11(addr);
#else
            return false;
#endif
        }
    }

    strncpy(addr->sun_path, sway_sock, sizeof(addr->sun_path) - 1);
    return true;
}

bool
i3_send_pkg(int sock, int cmd, char *data)
{
    const size_t size = data != NULL ? strlen(data) : 0;
    const i3_ipc_header_t hdr = {
        .magic = I3_IPC_MAGIC,
        .size = size,
        .type = cmd
    };

    if (write(sock, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
        return false;

    if (data != NULL) {
        if (write(sock, data, size) != (ssize_t)size)
            return false;
    }

    return true;
}

bool
i3_receive_loop(int abort_fd, int sock,
                const struct i3_ipc_callbacks *cbs, void *data)
{
    /* Initial reply typically requires a couple of KB. But we often
     * need more later. For example, switching workspaces can result
     * in quite big notification messages. */
    size_t reply_buf_size = 4096;
    char *buf = malloc(reply_buf_size);
    size_t buf_idx = 0;

    bool err = false;

    while (!err) {
        struct pollfd fds[] = {
            {.fd = abort_fd, .events = POLLIN},
            {.fd = sock, .events = POLLIN}
        };

        int res = poll(fds, 2, -1);
        if (res <= 0) {
            LOG_ERRNO("failed to poll()");
            err = true;
            break;
        }

        if (fds[0].revents & POLLIN) {
            LOG_DBG("aborted");
            break;
        }

        if (fds[1].revents & POLLHUP) {
            LOG_DBG("disconnected");
            break;
        }

        assert(fds[1].revents & POLLIN);

        /* Grow receive buffer, if necessary */
        if (buf_idx == reply_buf_size) {
            LOG_DBG("growing reply buffer: %zu -> %zu",
                    reply_buf_size, reply_buf_size * 2);

            char *new_buf = realloc(buf, reply_buf_size * 2);
            if (new_buf == NULL) {
                LOG_ERR("failed to grow reply buffer from %zu to %zu bytes",
                        reply_buf_size, reply_buf_size * 2);
                err = true;
                break;
            }

            buf = new_buf;
            reply_buf_size *= 2;
        }

        assert(reply_buf_size > buf_idx);

        ssize_t bytes = read(sock, &buf[buf_idx], reply_buf_size - buf_idx);
        if (bytes < 0) {
            LOG_ERRNO("failed to read from i3's socket");
            err = true;
            break;
        }

        buf_idx += bytes;

        while (!err && buf_idx >= sizeof(i3_ipc_header_t)) {
            const i3_ipc_header_t *hdr = (const i3_ipc_header_t *)buf;
            if (strncmp(hdr->magic, I3_IPC_MAGIC, sizeof(hdr->magic)) != 0) {
                LOG_ERR(
                    "i3 IPC header magic mismatch: expected \"%.*s\", got \"%.*s\"",
                    (int)sizeof(hdr->magic), I3_IPC_MAGIC,
                    (int)sizeof(hdr->magic), hdr->magic);

                err = true;
                break;
            }

            size_t total_size = sizeof(i3_ipc_header_t) + hdr->size;

            if (total_size > buf_idx) {
                LOG_DBG("got %zd bytes, need %zu", bytes, total_size);
                break;
            }

            /* Json-c expects a NULL-terminated string */
            char json_str[hdr->size + 1];
            memcpy(json_str, &buf[sizeof(*hdr)], hdr->size);
            json_str[hdr->size] = '\0';
            //printf("raw: %s\n", json_str);
            LOG_DBG("raw: %s\n", json_str);

            //json_tokener *tokener = json_tokener_new();
            struct json_object *json = json_tokener_parse(json_str);
            if (json == NULL) {
                LOG_ERR("failed to parse json");
                err = true;
                break;
            }

            //err = pkt_handler(hdr, json, data);
            i3_ipc_callback_t pkt_handler = NULL;
            switch (hdr->type) {
            case I3_IPC_REPLY_TYPE_COMMAND:
                pkt_handler = cbs->reply_command;
                break;
            case I3_IPC_REPLY_TYPE_WORKSPACES:
                pkt_handler = cbs->reply_workspaces;
                break;
            case I3_IPC_REPLY_TYPE_SUBSCRIBE:
                pkt_handler = cbs->reply_subscribe;
                break;
            case I3_IPC_REPLY_TYPE_OUTPUTS:
                pkt_handler = cbs->reply_outputs;
                break;
            case I3_IPC_REPLY_TYPE_TREE:
                pkt_handler = cbs->reply_tree;
                break;
            case I3_IPC_REPLY_TYPE_MARKS:
                pkt_handler = cbs->reply_marks;
                break;
            case I3_IPC_REPLY_TYPE_BAR_CONFIG:
                pkt_handler = cbs->reply_bar_config;
                break;
            case I3_IPC_REPLY_TYPE_VERSION:
                pkt_handler = cbs->reply_version;
                break;
            case I3_IPC_REPLY_TYPE_BINDING_MODES:
                pkt_handler = cbs->reply_binding_modes;
                break;
            case I3_IPC_REPLY_TYPE_CONFIG:
                pkt_handler = cbs->reply_config;
                break;
            case I3_IPC_REPLY_TYPE_TICK:
                pkt_handler = cbs->reply_tick;
                break;
#if defined(I3_IPC_REPLY_TYPE_SYNC)
            case I3_IPC_REPLY_TYPE_SYNC:
                pkt_handler = cbs->reply_sync;
                break;
#endif
            /* Sway extensions */
            case 100:  /* IPC_GET_INPUTS */
                pkt_handler = cbs->reply_inputs;
                break;

            case I3_IPC_EVENT_WORKSPACE:
                pkt_handler = cbs->event_workspace;
                break;
            case I3_IPC_EVENT_OUTPUT:
                pkt_handler = cbs->event_output;
                break;
            case I3_IPC_EVENT_MODE:
                pkt_handler = cbs->event_mode;
                break;
            case I3_IPC_EVENT_WINDOW:
                pkt_handler = cbs->event_window;
                break;
            case I3_IPC_EVENT_BARCONFIG_UPDATE:
                pkt_handler = cbs->event_barconfig_update;
                break;
            case I3_IPC_EVENT_BINDING:
                pkt_handler = cbs->event_binding;
                break;
            case I3_IPC_EVENT_SHUTDOWN:
                pkt_handler = cbs->event_shutdown;
                break;
            case I3_IPC_EVENT_TICK:
                pkt_handler = cbs->event_tick;
                break;

            default:
                LOG_ERR("unimplemented IPC reply type: %d", hdr->type);
                pkt_handler = NULL;
                break;
            }

            if (pkt_handler != NULL)
                err = !pkt_handler(hdr->type, json, data);
            else
                LOG_DBG("no handler for reply/event %d; ignoring", hdr->type);

            json_object_put(json);

            assert(total_size <= buf_idx);
            memmove(buf, &buf[total_size], buf_idx - total_size);
            buf_idx -= total_size;
        }

        if (cbs->burst_done != NULL)
            cbs->burst_done(data);
    }

    free(buf);
    return !err;
}
