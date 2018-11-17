#include "xcb.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
