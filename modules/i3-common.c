#include "i3-common.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#if defined(ENABLE_X11)
 #include <xcb/xcb.h>
 #include <xcb/xcb_aux.h>
#endif

#include <i3/ipc.h>

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
    size_t size = data != NULL ? strlen(data) : 0;
    i3_ipc_header_t hdr = {
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
