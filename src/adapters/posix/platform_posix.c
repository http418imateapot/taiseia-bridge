/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The POSIX platform: sockets, monotonic time, wall-clock timestamps.
 */
#define _POSIX_C_SOURCE 200809L

#include "adapters/platform.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Where the flag exists, SIGPIPE is suppressed per call. */
#ifdef MSG_NOSIGNAL
#  define SEND_FLAGS MSG_NOSIGNAL
#else
#  define SEND_FLAGS 0
#endif

result_t platform_init(void)
{
    /* Unconditionally, and in addition to MSG_NOSIGNAL: the default
     * disposition kills the process the first time a broker disappears
     * mid-write, and that is not a failure mode worth leaving to a macro
     * being defined on somebody else's libc. */
    signal(SIGPIPE, SIG_IGN);

    return RES_OK;
}

void platform_shutdown(void)
{
}

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

void platform_install_stop_handler(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

bool platform_stop_requested(void)
{
    return g_stop != 0;
}

void platform_request_stop(void)
{
    g_stop = 1;
}

int64_t platform_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool platform_iso8601_utc(char *out, size_t cap)
{
    struct timespec ts;
    struct tm       tm;
    int             n;

    if (!out || cap < 25u)
        return false;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return false;
    if (!gmtime_r(&ts.tv_sec, &tm))
        return false;

    n = snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec,
                 (int)(ts.tv_nsec / 1000000));

    return n > 0 && (size_t)n < cap;
}

void platform_sleep_ms(int ms)
{
    struct timespec ts;

    if (ms <= 0)
        return;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/* ------------------------------------------------------------------ socket */

static void ps_close(void *self)
{
    platform_socket_t *s = (platform_socket_t *)self;

    if (s->handle >= 0)
        close((int)s->handle);

    s->handle     = -1;
    s->connecting = false;
}

/*
 * The one place this adapter blocks.
 *
 * getaddrinfo is synchronous, so a slow or unreachable resolver stalls the
 * main loop for as long as it takes -- usually milliseconds, occasionally
 * seconds. Fixing it properly needs a resolver thread, which is a large
 * addition for a program that connects to one broker and then stays there;
 * the reconnect backoff bounds how often it can happen. Configure the broker
 * by address to avoid it entirely.
 */
static result_t begin_connect(platform_socket_t *s, const char *host,
                              uint16_t port)
{
    struct addrinfo  hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char             service[8];
    int              fd = -1;

    snprintf(service, sizeof(service), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, service, &hints, &res) != 0 || !res)
        return RES_EIO;

    for (ai = res; ai; ai = ai->ai_next) {
        int flags;

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;

        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            fd = -1;
            continue;
        }

        {
            int one = 1;

            /* Small packets that matter more than throughput. */
            (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        }

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            freeaddrinfo(res);
            s->handle     = fd;
            s->connecting = false;
            return RES_OK;
        }
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            freeaddrinfo(res);
            s->handle     = fd;
            s->connecting = true;
            return RES_EAGAIN;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return RES_EIO;
}

static result_t finish_connect(platform_socket_t *s)
{
    struct pollfd p;
    int           err = 0;
    socklen_t     len = sizeof(err);

    p.fd      = (int)s->handle;
    p.events  = POLLOUT;
    p.revents = 0;

    if (poll(&p, 1, 0) <= 0)
        return RES_EAGAIN;

    if (getsockopt((int)s->handle, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ||
        err != 0) {
        ps_close(s);
        return RES_EIO;
    }

    s->connecting = false;
    return RES_OK;
}

static result_t ps_connect(void *self, const char *host, uint16_t port)
{
    platform_socket_t *s = (platform_socket_t *)self;

    if (s->handle >= 0 && !s->connecting)
        return RES_OK;
    if (s->connecting)
        return finish_connect(s);

    return begin_connect(s, host, port);
}

static result_t ps_send(void *self, const uint8_t *buf, size_t n, size_t *sent)
{
    platform_socket_t *s = (platform_socket_t *)self;
    ssize_t            r;

    if (s->handle < 0 || s->connecting)
        return RES_EIO;

    r = send((int)s->handle, buf, n, SEND_FLAGS);
    if (r > 0) {
        *sent = (size_t)r;
        return RES_OK;
    }
    if (r == 0)
        return RES_EAGAIN;

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return RES_EAGAIN;

    return RES_EIO;
}

static result_t ps_recv(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    platform_socket_t *s = (platform_socket_t *)self;
    ssize_t            r;

    if (s->handle < 0 || s->connecting)
        return RES_EIO;

    r = recv((int)s->handle, buf, cap, 0);
    if (r > 0) {
        *got = (size_t)r;
        return RES_OK;
    }
    /* Zero means the peer closed, which is not the same as "nothing yet" and
     * must not be reported as it -- the caller would wait forever. */
    if (r == 0)
        return RES_EIO;

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return RES_EAGAIN;

    return RES_EIO;
}

static bool ps_connected(void *self)
{
    const platform_socket_t *s = (const platform_socket_t *)self;

    return s->handle >= 0 && !s->connecting;
}

static const socket_vt_t k_posix_vt = {
    ps_connect, ps_close, ps_send, ps_recv, ps_connected
};

void platform_socket_bind(platform_socket_t *s, socket_t *out)
{
    if (!s || !out)
        return;

    s->handle     = -1;
    s->connecting = false;

    out->vt   = &k_posix_vt;
    out->self = s;
}
