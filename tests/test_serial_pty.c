/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The serial adapter against a real serial device.
 *
 * A pseudo-terminal is a real tty: it has a line discipline, it echoes by
 * default, and it will translate carriage returns if you let it. That makes it
 * the right stand-in for a USB adapter here -- not because it behaves like one
 * electrically, but because it has all the same ways of quietly corrupting a
 * binary protocol, and those are what the adapter's setup code exists to turn
 * off.
 *
 * POSIX only. Windows has no equivalent that can be opened in CI, so its
 * serial adapter is still exercised only through the port interface by a fake,
 * and docs/testing.md says so.
 */
#ifndef _WIN32
#  define _XOPEN_SOURCE 700
#  define _DEFAULT_SOURCE
#endif

#include "tests.h"

#ifdef _WIN32

void test_serial_pty(void)
{
    /* Nothing to do: see the note above. */
}

#else

#include "adapters/platform.h"
#include "adapters/taiseia_uart/uart_link.h"
#include "core/taiseia/frame.h"
#include "ports/appliance_link.h"
#include "ports/serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

/* --------------------------------------------------------------- pty pair */

typedef struct {
    int  master;
    char slave_path[64];
} pty_t;

static bool pty_open(pty_t *p)
{
    const char *name;

    p->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (p->master < 0)
        return false;

    if (grantpt(p->master) != 0 || unlockpt(p->master) != 0) {
        close(p->master);
        p->master = -1;
        return false;
    }

    name = ptsname(p->master);
    if (!name) {
        close(p->master);
        p->master = -1;
        return false;
    }

    /* The master must not block either, or a test that expects silence would
     * wait for it forever. */
    (void)fcntl(p->master, F_SETFL, O_NONBLOCK);

    snprintf(p->slave_path, sizeof(p->slave_path), "%s", name);
    return true;
}

static void pty_close(pty_t *p)
{
    if (p->master >= 0)
        close(p->master);
    p->master = -1;
}

static ssize_t master_read(pty_t *p, uint8_t *buf, size_t cap, int tries)
{
    int i;

    for (i = 0; i < tries; i++) {
        ssize_t r = read(p->master, buf, cap);

        if (r > 0)
            return r;
        if (r == 0 || (errno != EAGAIN && errno != EINTR))
            return -1;
        platform_sleep_ms(2);
    }
    return 0;
}

/* --------------------------------------------------------------- fixtures */

static bool open_port(pty_t *p, platform_serial_t *ps, serial_t *s)
{
    serial_params_t params;

    if (!pty_open(p))
        return false;

    serial_params_defaults(&params);
    platform_serial_bind(ps, s);

    if (serial_open(s, p->slave_path, &params) != RES_OK) {
        pty_close(p);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ tests */

static void a_real_port_carries_bytes_both_ways(void)
{
    pty_t             p;
    platform_serial_t ps;
    serial_t          s;
    uint8_t           buf[64];
    size_t            n = 0;
    ssize_t           r;
    int               i;

    if (!open_port(&p, &ps, &s)) {
        CHECK(0 && "cannot open a pseudo-terminal");
        return;
    }
    CHECK(serial_is_open(&s));

    /* Port to peer. */
    CHECK_EQ(serial_write(&s, (const uint8_t *)"hello", 5, &n), RES_OK);
    CHECK_EQ(n, 5);
    r = master_read(&p, buf, sizeof(buf), 200);
    CHECK_EQ(r, 5);
    if (r == 5)
        CHECK(memcmp(buf, "hello", 5) == 0);

    /* Peer to port. */
    CHECK_EQ((int)write(p.master, "world", 5), 5);
    n = 0;
    for (i = 0; i < 200 && n == 0; i++) {
        if (serial_read(&s, buf, sizeof(buf), &n) == RES_EAGAIN)
            platform_sleep_ms(2);
    }
    CHECK_EQ(n, 5);
    if (n == 5)
        CHECK(memcmp(buf, "world", 5) == 0);

    serial_close(&s);
    CHECK(!serial_is_open(&s));
    pty_close(&p);
}

/*
 * A tty echoes by default. Left on, every byte we wrote would come back as
 * though the appliance had said it, and every reply would be read at the wrong
 * offset from then on. This is the single thing the adapter's setup code most
 * needs to have got right, and nothing but a real tty will show it.
 */
static void the_line_discipline_is_off(void)
{
    pty_t             p;
    platform_serial_t ps;
    serial_t          s;
    uint8_t           buf[64];
    size_t            n = 0;
    ssize_t           r;

    if (!open_port(&p, &ps, &s)) {
        CHECK(0 && "cannot open a pseudo-terminal");
        return;
    }

    /* Send the peer something. If echo were on, it would come straight back
     * to the peer as well. */
    CHECK_EQ((int)write(p.master, "abc", 3), 3);
    platform_sleep_ms(20);
    r = master_read(&p, buf, sizeof(buf), 5);
    CHECK_EQ(r, 0);                    /* nothing echoed */

    /* And the port still has the bytes, unmodified. */
    {
        int i;

        n = 0;
        for (i = 0; i < 200 && n == 0; i++) {
            if (serial_read(&s, buf, sizeof(buf), &n) == RES_EAGAIN)
                platform_sleep_ms(2);
        }
    }
    CHECK_EQ(n, 3);

    /*
     * Carriage returns, newlines and the byte a terminal treats as an
     * interrupt all pass through untouched. In cooked mode 0x0D would arrive
     * as 0x0A and 0x03 would raise a signal.
     */
    {
        static const uint8_t raw[] = { 0x00, 0x0D, 0x0A, 0x03, 0x1A, 0xFF };
        size_t               got = 0;
        int                  i;

        CHECK_EQ((int)write(p.master, raw, sizeof(raw)), (int)sizeof(raw));

        for (i = 0; i < 200 && got < sizeof(raw); i++) {
            size_t   part = 0;
            result_t rr = serial_read(&s, buf + got, sizeof(buf) - got, &part);

            if (rr == RES_OK)
                got += part;
            else
                platform_sleep_ms(2);
        }

        CHECK_EQ(got, sizeof(raw));
        if (got == sizeof(raw))
            CHECK(memcmp(buf, raw, sizeof(raw)) == 0);
    }

    serial_close(&s);
    pty_close(&p);
}

/*
 * The whole southbound path over a real device: the adapter builds a request,
 * the port carries it, an appliance played by the other end of the pty
 * answers, and the reply comes back decoded.
 */
static void a_full_exchange_over_a_real_port(void)
{
    pty_t             p;
    platform_serial_t ps;
    serial_t          s;
    uart_link_t       u;
    link_t            l;
    link_req_t        req;
    link_resp_t       resp;
    uint8_t           seen[16];
    size_t            seen_len = 0;
    bool              answered = false;
    result_t          r = RES_EAGAIN;
    int               i;

    if (!open_port(&p, &ps, &s)) {
        CHECK(0 && "cannot open a pseudo-terminal");
        return;
    }

    CHECK_EQ(uart_link_init(&u, &s, platform_now_ms), RES_OK);
    uart_link_bind(&u, &l);
    CHECK(link_is_up(&l));

    memset(&req, 0, sizeof(req));
    req.id = 42; req.op = LINK_OP_READ; req.sa_id = 0x04; req.service_id = 0x07;

    CHECK_EQ(link_submit(&l, &req), RES_OK);

    /*
     * Play the appliance in the same loop that drives the link, so the reply
     * goes out the instant the request lands. The protocol allows 200 ms and a
     * loopback needs microseconds, but a loaded build machine should not be
     * what decides whether this passes.
     */
    for (i = 0; i < 400; i++) {
        r = link_poll(&l, &resp);
        if (r != RES_EAGAIN)
            break;

        if (!answered) {
            ssize_t got = read(p.master, seen + seen_len,
                               sizeof(seen) - seen_len);

            if (got > 0)
                seen_len += (size_t)got;

            /* A six-byte write usually lands whole, but a tty is under no
             * obligation to make it so. */
            if (seen_len >= 6) {
                uint8_t reply[6];

                CHECK_EQ(seen[0], 0x06);
                CHECK_EQ(seen[1], 0x04);
                CHECK_EQ(seen[2], 0x07);
                CHECK_EQ(seen[5], ts_checksum(seen, 5));

                reply[0] = 0x06;
                reply[1] = 0x04;
                reply[2] = 0x07;
                reply[3] = 0x00;
                reply[4] = 0x2A;
                reply[5] = ts_checksum(reply, 5);

                CHECK_EQ((int)write(p.master, reply, 6), 6);
                answered = true;
            }
        }
        platform_sleep_ms(1);
    }

    CHECK(answered);
    CHECK_EQ(r, RES_OK);
    if (r == RES_OK) {
        CHECK_EQ(resp.id, 42);
        CHECK_EQ(resp.status, RES_OK);
        CHECK_EQ(resp.service_id, 0x07);
        CHECK_EQ(resp.data_len, 2);
        CHECK_EQ((resp.data[0] << 8) | resp.data[1], 42);
    }

    link_close(&l);
    pty_close(&p);
}

/* A silent peer produces a timeout, not a wait that never ends. */
static void a_silent_peer_times_out_over_a_real_port(void)
{
    pty_t             p;
    platform_serial_t ps;
    serial_t          s;
    uart_link_t       u;
    link_t            l;
    link_req_t        req;
    link_resp_t       resp;
    result_t          r = RES_EAGAIN;
    int64_t           started;
    int               i;

    if (!open_port(&p, &ps, &s)) {
        CHECK(0 && "cannot open a pseudo-terminal");
        return;
    }

    CHECK_EQ(uart_link_init(&u, &s, platform_now_ms), RES_OK);
    uart_link_bind(&u, &l);

    memset(&req, 0, sizeof(req));
    req.id = 1; req.op = LINK_OP_READ; req.sa_id = 0x04; req.service_id = 0x07;

    started = platform_now_ms();
    CHECK_EQ(link_submit(&l, &req), RES_OK);

    for (i = 0; i < 2000; i++) {
        r = link_poll(&l, &resp);
        if (r != RES_EAGAIN)
            break;
        platform_sleep_ms(1);
    }

    CHECK_EQ(r, RES_OK);
    if (r == RES_OK)
        CHECK_EQ(resp.status, RES_ETIMEOUT);

    /* And it gave up on the protocol's schedule, not on some other one. */
    {
        int64_t took = platform_now_ms() - started;

        CHECK(took >= TS_RESPONSE_TIMEOUT_MS);
        CHECK(took < TS_RESPONSE_TIMEOUT_MS + 2000);
    }

    link_close(&l);
    pty_close(&p);
}

static void opening_refuses_what_it_should(void)
{
    pty_t             p;
    platform_serial_t ps;
    serial_t          s;
    serial_params_t   params;

    serial_params_defaults(&params);
    platform_serial_bind(&ps, &s);

    CHECK_EQ(serial_open(&s, "/dev/tty-there-is-no-such-thing", &params),
             RES_ENOTFOUND);
    CHECK(!serial_is_open(&s));

    /* A baud rate with no constant, and a framing this adapter will not do. */
    if (pty_open(&p)) {
        params.baud = 12345;
        CHECK_EQ(serial_open(&s, p.slave_path, &params), RES_EINVAL);

        serial_params_defaults(&params);
        params.data_bits = 7;
        CHECK_EQ(serial_open(&s, p.slave_path, &params), RES_EUNSUPPORTED);

        serial_params_defaults(&params);
        params.parity = 'X';
        CHECK_EQ(serial_open(&s, p.slave_path, &params), RES_EINVAL);

        /* Opening one twice would leave the first descriptor unreachable. */
        serial_params_defaults(&params);
        CHECK_EQ(serial_open(&s, p.slave_path, &params), RES_OK);
        CHECK_EQ(serial_open(&s, p.slave_path, &params), RES_EBUSY);
        CHECK_EQ(serial_flush(&s), RES_OK);

        serial_close(&s);
        pty_close(&p);
    }

    /* And the dispatcher refuses what it cannot forward. */
    {
        size_t  n = 0;
        uint8_t b[4];

        CHECK_EQ(serial_open(NULL, "x", &params), RES_EINVAL);
        CHECK_EQ(serial_read(&s, b, sizeof(b), &n), RES_EIO);
        CHECK_EQ(serial_write(&s, b, 1, &n), RES_EIO);
        CHECK_EQ(serial_flush(&s), RES_EIO);
        CHECK(!serial_is_open(NULL));
        serial_close(NULL);
    }
}

void test_serial_pty(void)
{
    a_real_port_carries_bytes_both_ways();
    the_line_discipline_is_off();
    a_full_exchange_over_a_real_port();
    a_silent_peer_times_out_over_a_real_port();
    opening_refuses_what_it_should();
}

#endif /* _WIN32 */
