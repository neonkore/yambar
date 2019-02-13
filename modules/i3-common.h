#pragma once

#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

bool i3_get_socket_address(struct sockaddr_un *addr);
