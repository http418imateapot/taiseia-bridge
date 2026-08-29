/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Windows platform: sockets, monotonic time, wall-clock timestamps.
 *
 * Deliberately the same shape as the POSIX file next to it, so that the two
 * can be read side by side and a difference stands out as a decision rather
 * than as drift.
 */
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601      /* getaddrinfo wants Vista or later */
#endif

#include "adapters/platform.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define HANDLE_CLOSED ((intptr_t)-1)

static SOCKET as_socket(intptr_t h)
{
    return (SOCKET)h;
}

result_t platform_init(void)
{
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return RES_EIO;

    return RES_OK;
}

void platform_shutdown(void)
{
    WSACleanup();
}

int64_t platform_now_ms(void)
{
    /* Monotonic and unaffected by the system clock being stepped, which is
     * the whole point: a timer must not fire years early because NTP arrived. */
    return (int64_t)GetTickCount64();
}

bool platform_iso8601_utc(char *out, size_t cap)
{
    SYSTEMTIME st;
    int        n;

    if (!out || cap < 25u)
        return false;

    GetSystemTime(&st);

    n = snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 (int)st.wYear, (int)st.wMonth, (int)st.wDay,
                 (int)st.wHour, (int)st.wMinute, (int)st.wSecond,
                 (int)st.wMilliseconds);

    return n > 0 && (size_t)n < cap;
}

void platform_sleep_ms(int ms)
{
    if (ms > 0)
        Sleep((DWORD)ms);
}

/* ------------------------------------------------------------------ socket */

static void ws_close(void *self)
{
    platform_socket_t *s = (platform_socket_t *)self;

    if (s->handle != HANDLE_CLOSED)
        closesocket(as_socket(s->handle));

    s->handle     = HANDLE_CLOSED;
    s->connecting = false;
}

/* Synchronous, exactly as on POSIX and with the same caveat: see the note in
 * platform_posix.c. */
static result_t begin_connect(platform_socket_t *s, const char *host,
                              uint16_t port)
{
    struct addrinfo  hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char             service[8];

    snprintf(service, sizeof(service), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, service, &hints, &res) != 0 || !res)
        return RES_EIO;

    for (ai = res; ai; ai = ai->ai_next) {
        SOCKET fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        u_long nonblocking = 1;
        int    one = 1;

        if (fd == INVALID_SOCKET)
            continue;

        if (ioctlsocket(fd, FIONBIO, &nonblocking) != 0) {
            closesocket(fd);
            continue;
        }

        /* Small packets that matter more than throughput. */
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
                         sizeof(one));

        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
            freeaddrinfo(res);
            s->handle     = (intptr_t)fd;
            s->connecting = false;
            return RES_OK;
        }
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            freeaddrinfo(res);
            s->handle     = (intptr_t)fd;
            s->connecting = true;
            return RES_EAGAIN;
        }

        closesocket(fd);
    }

    freeaddrinfo(res);
    return RES_EIO;
}

static result_t finish_connect(platform_socket_t *s)
{
    fd_set         wr;
    fd_set         ex;
    struct timeval tv;
    SOCKET         fd = as_socket(s->handle);
    int            n;

    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET(fd, &wr);
    FD_SET(fd, &ex);

    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    n = select(0, NULL, &wr, &ex, &tv);
    if (n <= 0)
        return RES_EAGAIN;

    /* Winsock reports a failed connect through the exception set rather than
     * through writability, so both have to be watched. */
    if (FD_ISSET(fd, &ex)) {
        ws_close(s);
        return RES_EIO;
    }

    {
        int err = 0;
        int len = (int)sizeof(err);

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 ||
            err != 0) {
            ws_close(s);
            return RES_EIO;
        }
    }

    s->connecting = false;
    return RES_OK;
}

static result_t ws_connect(void *self, const char *host, uint16_t port)
{
    platform_socket_t *s = (platform_socket_t *)self;

    if (s->handle != HANDLE_CLOSED && !s->connecting)
        return RES_OK;
    if (s->connecting)
        return finish_connect(s);

    return begin_connect(s, host, port);
}

static result_t ws_send(void *self, const uint8_t *buf, size_t n, size_t *sent)
{
    platform_socket_t *s = (platform_socket_t *)self;
    int                r;

    if (s->handle == HANDLE_CLOSED || s->connecting)
        return RES_EIO;
    if (n > (size_t)INT_MAX)
        n = (size_t)INT_MAX;

    r = send(as_socket(s->handle), (const char *)buf, (int)n, 0);
    if (r > 0) {
        *sent = (size_t)r;
        return RES_OK;
    }
    if (r == 0)
        return RES_EAGAIN;

    if (WSAGetLastError() == WSAEWOULDBLOCK)
        return RES_EAGAIN;

    return RES_EIO;
}

static result_t ws_recv(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    platform_socket_t *s = (platform_socket_t *)self;
    int                r;

    if (s->handle == HANDLE_CLOSED || s->connecting)
        return RES_EIO;
    if (cap > (size_t)INT_MAX)
        cap = (size_t)INT_MAX;

    r = recv(as_socket(s->handle), (char *)buf, (int)cap, 0);
    if (r > 0) {
        *got = (size_t)r;
        return RES_OK;
    }
    /* Zero means the peer closed, which is not the same as "nothing yet" and
     * must not be reported as it -- the caller would wait forever. */
    if (r == 0)
        return RES_EIO;

    if (WSAGetLastError() == WSAEWOULDBLOCK)
        return RES_EAGAIN;

    return RES_EIO;
}

static bool ws_connected(void *self)
{
    const platform_socket_t *s = (const platform_socket_t *)self;

    return s->handle != HANDLE_CLOSED && !s->connecting;
}

static const socket_vt_t k_win32_vt = {
    ws_connect, ws_close, ws_send, ws_recv, ws_connected
};

void platform_socket_bind(platform_socket_t *s, socket_t *out)
{
    if (!s || !out)
        return;

    s->handle     = HANDLE_CLOSED;
    s->connecting = false;

    out->vt   = &k_win32_vt;
    out->self = s;
}
