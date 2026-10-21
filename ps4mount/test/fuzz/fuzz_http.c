/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Fuzz target: requests to the web panel (anyone on the network can send them). */
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "web.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
        return 0;
    int big = 1 << 20;
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &big, sizeof(big));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &big, sizeof(big));
    if (size > 65536)
        size = 65536;
    if (size && write(sv[0], data, size) < 0) {
        close(sv[0]);
        close(sv[1]);
        return 0;
    }
    shutdown(sv[0], SHUT_WR);
    web_handle(sv[1]);
    char buf[65536];
    while (read(sv[0], buf, sizeof(buf)) > 0)
        ;
    close(sv[0]);
    return 0;
}
