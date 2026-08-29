/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The platform adapters against a real loopback listener.
 *
 * These are the only tests in the suite that touch the operating system, and
 * they are worth it: a socket adapter is short but every line of it is a place
 * to conflate "nothing yet" with "the peer is gone", which no amount of testing
 * against a fake will find.
 *
 * The listener is written twice, once per platform, because that is the honest
 * cost of testing a platform seam from outside it.
 */
#include "tests.h"

#include "adapters/platform.h"
#include "ports/socket.h"

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET raw_sock_t;
#  define RAW_INVALID INVALID_SOCKET
#  define raw_close(s) closesocket(s)
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int raw_sock_t;
#  define RAW_INVALID (-1)
#  define raw_close(s) close(s)
#endif

/* ---------------------------------------------------------------- listener */

static raw_sock_t listen_open(uint16_t *port)
{
    raw_sock_t         fd;
    struct sockaddr_in a;
#ifdef _WIN32
    int len = (int)sizeof(a);
#else
    socklen_t len = sizeof(a);
#endif

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == RAW_INVALID)
        return RAW_INVALID;

    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;               /* let the system choose */

    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(fd, 4) != 0 ||
        getsockname(fd, (struct sockaddr *)&a, &len) != 0) {
        raw_close(fd);
        return RAW_INVALID;
    }

    *port = ntohs(a.sin_port);
    return fd;
}

/* An ephemeral port that nothing is listening on: bind one, learn it, drop it. */
static uint16_t dead_port(void)
{
    uint16_t   p = 0;
    raw_sock_t fd = listen_open(&p);

    if (fd != RAW_INVALID)
        raw_close(fd);

    return p;
}

/* Drive the port's connect loop until it settles or the budget runs out. */
static result_t connect_within(socket_t *s, const char *host, uint16_t port,
                               int tries)
{
    int i;

    for (i = 0; i < tries; i++) {
        result_t r = sock_connect(s, host, port);

        if (r != RES_EAGAIN)
            return r;
        platform_sleep_ms(5);
    }
    return RES_ETIMEOUT;
}

static result_t recv_within(socket_t *s, uint8_t *buf, size_t cap, size_t *got,
                            int tries)
{
    int i;

    for (i = 0; i < tries; i++) {
        result_t r = sock_recv(s, buf, cap, got);

        if (r != RES_EAGAIN)
            return r;
        platform_sleep_ms(5);
    }
    return RES_ETIMEOUT;
}

/* ------------------------------------------------------------------- tests */

static void time_moves_forward_and_formats(void)
{
    char    stamp[PLATFORM_TIMESTAMP_MAX];
    int64_t a, b;

    a = platform_now_ms();
    platform_sleep_ms(60);
    b = platform_now_ms();

    CHECK(b >= a);
    /* Generous on both sides: schedulers are not precise, and one platform's
     * tick counter has about 15 ms of granularity. */
    CHECK(b - a >= 15);
    CHECK(b - a < 5000);

    CHECK(platform_iso8601_utc(stamp, sizeof(stamp)));
    CHECK_EQ(strlen(stamp), 24);
    CHECK_EQ(stamp[4], '-');
    CHECK_EQ(stamp[7], '-');
    CHECK_EQ(stamp[10], 'T');
    CHECK_EQ(stamp[13], ':');
    CHECK_EQ(stamp[19], '.');
    CHECK_EQ(stamp[23], 'Z');

    /* A buffer that cannot hold it is refused rather than half-filled. */
    CHECK(!platform_iso8601_utc(stamp, 8));
    CHECK(!platform_iso8601_utc(NULL, sizeof(stamp)));
}

static void log_levels_parse(void)
{
    CHECK_EQ(log_level_from_str("error"), LOG_ERROR);
    CHECK_EQ(log_level_from_str("debug"), LOG_DEBUG);
    CHECK_EQ(log_level_from_str("nonsense"), LOG_INFO);
    CHECK_EQ(log_level_from_str(NULL), LOG_INFO);
    CHECK(log_level_valid("warn"));
    CHECK(!log_level_valid("shout"));
    CHECK(!log_level_valid(NULL));
}

static void a_real_connection_carries_bytes_both_ways(void)
{
    raw_sock_t        lst;
    raw_sock_t        srv;
    uint16_t          port = 0;
    platform_socket_t ps;
    socket_t          s;
    uint8_t           buf[64];
    size_t            n = 0;

    lst = listen_open(&port);
    CHECK(lst != RAW_INVALID);
    if (lst == RAW_INVALID)
        return;

    platform_socket_bind(&ps, &s);
    CHECK(!sock_is_connected(&s));

    CHECK_EQ(connect_within(&s, "127.0.0.1", port, 400), RES_OK);
    CHECK(sock_is_connected(&s));

    srv = accept(lst, NULL, NULL);
    CHECK(srv != RAW_INVALID);
    if (srv == RAW_INVALID) {
        sock_close(&s);
        raw_close(lst);
        return;
    }

    /* Client to server. */
    CHECK_EQ(sock_send(&s, (const uint8_t *)"hello", 5, &n), RES_OK);
    CHECK_EQ(n, 5);
    {
        char got[16];
        int  r = (int)recv(srv, got, 5, 0);

        CHECK_EQ(r, 5);
        if (r == 5)
            CHECK(memcmp(got, "hello", 5) == 0);
    }

    /* Server to client. */
    CHECK_EQ((int)send(srv, "world", 5, 0), 5);
    CHECK_EQ(recv_within(&s, buf, sizeof(buf), &n, 400), RES_OK);
    CHECK_EQ(n, 5);
    CHECK(memcmp(buf, "world", 5) == 0);

    /*
     * A peer that closes is not "nothing yet". Conflating the two is the
     * defect this adapter is most likely to have, and the caller would wait
     * for a message that is never coming.
     */
    raw_close(srv);
    CHECK_EQ(recv_within(&s, buf, sizeof(buf), &n, 400), RES_EIO);

    sock_close(&s);
    CHECK(!sock_is_connected(&s));
    /* Closing twice is not an error. */
    sock_close(&s);

    raw_close(lst);
}

static void connecting_to_nothing_fails_rather_than_hangs(void)
{
    platform_socket_t ps;
    socket_t          s;
    uint16_t          port = dead_port();

    CHECK(port != 0);

    platform_socket_bind(&ps, &s);
    CHECK_EQ(connect_within(&s, "127.0.0.1", port, 400), RES_EIO);
    CHECK(!sock_is_connected(&s));

    sock_close(&s);
}

static void a_name_that_does_not_resolve_fails(void)
{
    platform_socket_t ps;
    socket_t          s;

    platform_socket_bind(&ps, &s);
    /* .invalid is reserved by RFC 2606 precisely so that it never resolves. */
    CHECK_EQ(sock_connect(&s, "nothing.invalid", 1883), RES_EIO);
    sock_close(&s);
}

/* Operations on a socket that is not open are errors, not crashes. */
static void a_closed_socket_refuses_work(void)
{
    platform_socket_t ps;
    socket_t          s;
    uint8_t           buf[8];
    size_t            n = 0;

    platform_socket_bind(&ps, &s);

    CHECK_EQ(sock_send(&s, (const uint8_t *)"x", 1, &n), RES_EIO);
    CHECK_EQ(sock_recv(&s, buf, sizeof(buf), &n), RES_EIO);
    CHECK(!sock_is_connected(&s));

    CHECK_EQ(sock_send(NULL, (const uint8_t *)"x", 1, &n), RES_EINVAL);
    CHECK_EQ(sock_recv(&s, NULL, 1, &n), RES_EINVAL);
    CHECK_EQ(sock_connect(&s, NULL, 1), RES_EINVAL);
    CHECK(!sock_is_connected(NULL));
    sock_close(NULL);

    /* Zero-length transfers are a no-op, not a protocol event. */
    CHECK_EQ(sock_send(&s, (const uint8_t *)"x", 0, &n), RES_OK);
    CHECK_EQ(n, 0);
    CHECK_EQ(sock_recv(&s, buf, 0, &n), RES_EAGAIN);
}

void test_platform(void)
{
    CHECK_EQ(platform_init(), RES_OK);

    time_moves_forward_and_formats();
    log_levels_parse();
    a_real_connection_carries_bytes_both_ways();
    connecting_to_nothing_fails_rather_than_hangs();
    a_name_that_does_not_resolve_fails();
    a_closed_socket_refuses_work();

    platform_shutdown();
}
