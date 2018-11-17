#pragma once

#include <xcb/xcb.h>

xcb_atom_t get_atom(xcb_connection_t *conn, const char *name);
char * get_atom_name(xcb_connection_t *conn, xcb_atom_t atom);
