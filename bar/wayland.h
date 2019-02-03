#pragma once

#include "backend.h"

extern const struct backend wayland_backend_iface;

void *bar_backend_wayland_new(void);
