#pragma once

#include <stdbool.h>
#include <xcb/xcb.h>

bool xcb_init(void);

xcb_atom_t get_atom(xcb_connection_t *conn, const char *name);
char *get_atom_name(xcb_connection_t *conn, xcb_atom_t atom);

const char *xcb_error(const xcb_generic_error_t *error);

/* Cached atoms */
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
