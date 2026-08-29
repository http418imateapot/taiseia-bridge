/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A stream socket, non-blocking throughout.
 *
 * CONNECTING IS A LOOP, NOT A CALL. `connect` starts the attempt and returns
 * RES_EAGAIN until it finishes; the caller calls it again on the next tick.
 * A blocking connect would be thirty fewer lines per adapter and would stall
 * the main loop for seconds on every unreachable broker, which is exactly the
 * property this program is built not to have.
 *
 * `send` and `recv` are partial by contract: they report how much moved and
 * return RES_EAGAIN when nothing can. A caller that assumes a send took
 * everything will corrupt the stream on the first busy socket.
 */
#ifndef TSB_PORTS_SOCKET_H
#define TSB_PORTS_SOCKET_H

#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    /**
     * Start, or continue, connecting to host:port.
     *
     * RES_OK once established, RES_EAGAIN while in progress, RES_EIO when the
     * attempt failed. Calling it again after RES_EIO starts a fresh attempt.
     */
    result_t (*connect)(void *self, const char *host, uint16_t port);

    /** Drop the connection. Safe on one already closed. */
    void (*close)(void *self);

    /**
     * Send what fits. `*sent` may be less than `n`, including zero.
     * RES_EAGAIN when nothing could be sent, RES_EIO when the peer is gone.
     */
    result_t (*send)(void *self, const uint8_t *buf, size_t n, size_t *sent);

    /**
     * Receive what is waiting. RES_EAGAIN when nothing is, RES_EIO when the
     * peer closed -- which is distinct from "nothing yet" and must not be
     * conflated with it.
     */
    result_t (*recv)(void *self, uint8_t *buf, size_t cap, size_t *got);

    bool (*is_connected)(void *self);
} socket_vt_t;

typedef struct {
    const socket_vt_t *vt;
    void              *self;
} socket_t;

result_t sock_connect(socket_t *s, const char *host, uint16_t port);
void     sock_close(socket_t *s);
result_t sock_send(socket_t *s, const uint8_t *buf, size_t n, size_t *sent);
result_t sock_recv(socket_t *s, uint8_t *buf, size_t cap, size_t *got);
bool     sock_is_connected(socket_t *s);

#endif /* TSB_PORTS_SOCKET_H */
