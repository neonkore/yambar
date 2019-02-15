#pragma once

#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

//#include <i3/ipc.h>
#include <json-c/json_util.h>

bool i3_get_socket_address(struct sockaddr_un *addr);
bool i3_send_pkg(int sock, int cmd, char *data);

typedef bool (*i3_ipc_callback_t)(int type, const struct json_object *json, void *data);

struct i3_ipc_callbacks {
    void (*burst_done)(void *data);

    i3_ipc_callback_t reply_command;
    i3_ipc_callback_t reply_workspaces;
    i3_ipc_callback_t reply_subscribe;
    i3_ipc_callback_t reply_outputs;
    i3_ipc_callback_t reply_tree;
    i3_ipc_callback_t reply_marks;
    i3_ipc_callback_t reply_bar_config;
    i3_ipc_callback_t reply_version;
    i3_ipc_callback_t reply_binding_modes;
    i3_ipc_callback_t reply_config;
    i3_ipc_callback_t reply_tick;
    i3_ipc_callback_t reply_sync;

    i3_ipc_callback_t event_workspace;
    i3_ipc_callback_t event_output;
    i3_ipc_callback_t event_mode;
    i3_ipc_callback_t event_window;
    i3_ipc_callback_t event_barconfig_update;
    i3_ipc_callback_t event_binding;
    i3_ipc_callback_t event_shutdown;
    i3_ipc_callback_t event_tick;
};

bool i3_receive_loop(
    int abort_fd, int sock,
    const struct i3_ipc_callbacks *callbacks, void *data);
