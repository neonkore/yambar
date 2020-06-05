#pragma once

#include <stdbool.h>
#include <xcb/xcb.h>

bool xcb_init(void);

xcb_atom_t get_atom(xcb_connection_t *conn, const char *name);
char *get_atom_name(xcb_connection_t *conn, xcb_atom_t atom);

const char *xcb_error(const xcb_generic_error_t *error);

/* Cached atoms */
extern xcb_atom_t UTF8_STRING;
extern xcb_atom_t _NET_WM_PID;
extern xcb_atom_t _NET_WM_WINDOW_TYPE;
extern xcb_atom_t _NET_WM_WINDOW_TYPE_DOCK;
extern xcb_atom_t _NET_WM_STATE;
extern xcb_atom_t _NET_WM_STATE_ABOVE;
extern xcb_atom_t _NET_WM_STATE_STICKY;
extern xcb_atom_t _NET_WM_DESKTOP;
extern xcb_atom_t _NET_WM_STRUT;
extern xcb_atom_t _NET_WM_STRUT_PARTIAL;
extern xcb_atom_t _NET_ACTIVE_WINDOW;
extern xcb_atom_t _NET_CURRENT_DESKTOP;
extern xcb_atom_t _NET_WM_VISIBLE_NAME;
extern xcb_atom_t _NET_WM_NAME;
