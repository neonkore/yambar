#pragma once

#include "backend.h"

extern const struct backend xcb_backend_iface;

void *bar_backend_xcb_new(void);
