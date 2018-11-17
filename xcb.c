#include "xcb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/render.h>

xcb_atom_t UTF8_STRING;
xcb_atom_t _NET_WM_PID;
xcb_atom_t _NET_WM_WINDOW_TYPE;
xcb_atom_t _NET_WM_WINDOW_TYPE_DOCK;
xcb_atom_t _NET_WM_STATE;
xcb_atom_t _NET_WM_STATE_ABOVE;
xcb_atom_t _NET_WM_STATE_STICKY;
xcb_atom_t _NET_WM_DESKTOP;
xcb_atom_t _NET_WM_STRUT;
xcb_atom_t _NET_WM_STRUT_PARTIAL;
xcb_atom_t _NET_ACTIVE_WINDOW;
xcb_atom_t _NET_CURRENT_DESKTOP;
xcb_atom_t _NET_WM_VISIBLE_NAME;
xcb_atom_t _NET_WM_NAME;

bool
xcb_init(void)
{
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

    /* Cache atoms */
    UTF8_STRING = get_atom(conn, "UTF8_STRING");
    _NET_WM_PID = get_atom(conn, "_NET_WM_PID");
    _NET_WM_WINDOW_TYPE = get_atom(conn,  "_NET_WM_WINDOW_TYPE");
    _NET_WM_WINDOW_TYPE_DOCK = get_atom(conn, "_NET_WM_WINDOW_TYPE_DOCK");
    _NET_WM_STATE = get_atom(conn, "_NET_WM_STATE");
    _NET_WM_STATE_ABOVE = get_atom(conn, "_NET_WM_STATE_ABOVE");
    _NET_WM_STATE_STICKY = get_atom(conn, "_NET_WM_STATE_STICKY");
    _NET_WM_DESKTOP = get_atom(conn, "_NET_WM_DESKTOP");
    _NET_WM_STRUT = get_atom(conn, "_NET_WM_STRUT");
    _NET_WM_STRUT_PARTIAL = get_atom(conn, "_NET_WM_STRUT_PARTIAL");
    _NET_ACTIVE_WINDOW = get_atom(conn, "_NET_ACTIVE_WINDOW");
    _NET_CURRENT_DESKTOP = get_atom(conn, "_NET_CURRENT_DESKTOP");
    _NET_WM_VISIBLE_NAME = get_atom(conn, "_NET_WM_VISIBLE_NAME");
    _NET_WM_NAME = get_atom(conn, "_NET_WM_NAME");
    _NET_WM_PID = get_atom(conn, "_NET_WM_PID");

    xcb_disconnect(conn);

    return true;
}

xcb_atom_t
get_atom(xcb_connection_t *conn, const char *name)
{
    xcb_generic_error_t *e;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        conn,
        xcb_intern_atom(conn, 1, strlen(name), name),
        &e);
    assert(e == NULL);

    xcb_atom_t ret = reply->atom;
    free(reply);
    return ret;
}

char *
get_atom_name(xcb_connection_t *conn, xcb_atom_t atom)
{
    xcb_generic_error_t *e;
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(
        conn, xcb_get_atom_name(conn, atom), &e);
    assert(e == NULL);

    int len = xcb_get_atom_name_name_length(reply);
    char *name = malloc(len + 1);
    memcpy(name, xcb_get_atom_name_name(reply), len);
    name[len] = '\0';

    free(reply);
    return name;
}
