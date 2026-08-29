/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dispatch for the socket port, so a null link or an unset entry point fails
 * as a typed result at the call site rather than as a call through nothing.
 */
#include "ports/socket.h"

result_t sock_connect(socket_t *s, const char *host, uint16_t port)
{
    if (!s || !s->vt || !s->vt->connect || !host)
        return RES_EINVAL;

    return s->vt->connect(s->self, host, port);
}

void sock_close(socket_t *s)
{
    if (!s || !s->vt || !s->vt->close)
        return;

    s->vt->close(s->self);
}

result_t sock_send(socket_t *s, const uint8_t *buf, size_t n, size_t *sent)
{
    if (!s || !s->vt || !s->vt->send || !buf || !sent)
        return RES_EINVAL;

    *sent = 0;
    if (n == 0)
        return RES_OK;

    return s->vt->send(s->self, buf, n, sent);
}

result_t sock_recv(socket_t *s, uint8_t *buf, size_t cap, size_t *got)
{
    if (!s || !s->vt || !s->vt->recv || !buf || !got)
        return RES_EINVAL;

    *got = 0;
    if (cap == 0)
        return RES_EAGAIN;

    return s->vt->recv(s->self, buf, cap, got);
}

bool sock_is_connected(socket_t *s)
{
    if (!s || !s->vt || !s->vt->is_connected)
        return false;

    return s->vt->is_connected(s->self);
}
