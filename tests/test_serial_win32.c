/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Windows serial adapter, as far as a machine with no serial port can
 * take it.
 *
 * WHAT THIS CAN AND CANNOT ESTABLISH.
 *
 * There is no Windows equivalent of a pseudo-terminal. A virtual COM pair
 * needs a kernel-mode driver, and every free one is test-signed -- which means
 * enabling test signing and rebooting, and a hosted CI runner cannot reboot in
 * the middle of a job. So the byte-carrying half of this adapter has never run
 * against a device in CI and, short of hardware, will not.
 *
 * What runs here is the part that needs no port: argument validation, the
 * refusal of a handle that is not a serial port, and the state of a closed
 * port. Those are real Win32 calls on a real Windows machine, which is more
 * than a fake can say -- and it is still a long way short of "the Windows
 * serial adapter works".
 *
 * The rest is closable by anyone with a com0com pair or two USB adapters and
 * a null-modem cable: set TSB_TEST_COM_PAIR to the two port names, for example
 *
 *     set TSB_TEST_COM_PAIR=COM11,COM12
 *
 * and the full exchange below runs against them. docs/testing.md says what is
 * open and how to close it.
 */
#include "tests.h"

#ifndef _WIN32

void test_serial_win32(void)
{
    /* Nothing to do: see the note above. */
}

#else

#include "adapters/platform.h"
#include "adapters/taiseia_uart/uart_link.h"
#include "core/taiseia/frame.h"
#include "ports/appliance_link.h"
#include "ports/serial.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- no port needed */

static void bad_parameters_are_refused_before_opening_anything(void)
{
    platform_serial_t s;
    serial_t          sp;
    serial_params_t   p;

    platform_serial_bind(&s, &sp);

    serial_params_defaults(&p);
    p.data_bits = 7;
    CHECK_EQ(serial_open(&sp, "COM1", &p), RES_EUNSUPPORTED);
    CHECK(!serial_is_open(&sp));

    serial_params_defaults(&p);
    p.stop_bits = 2;
    CHECK_EQ(serial_open(&sp, "COM1", &p), RES_EUNSUPPORTED);
    CHECK(!serial_is_open(&sp));

    serial_params_defaults(&p);
    p.baud = 300;
    CHECK_EQ(serial_open(&sp, "COM1", &p), RES_EINVAL);

    serial_params_defaults(&p);
    p.baud = 115200;
    CHECK_EQ(serial_open(&sp, "COM1", &p), RES_EINVAL);

    CHECK(!serial_is_open(&sp));
}

/* A port that is not there is reported as absent, not as a transport failure:
 * one is a configuration mistake and the other is a cable. */
static void an_absent_port_is_reported_absent(void)
{
    platform_serial_t s;
    serial_t          sp;
    serial_params_t   p;

    platform_serial_bind(&s, &sp);
    serial_params_defaults(&p);

    CHECK_EQ(serial_open(&sp, "COM99", &p), RES_ENOTFOUND);
    CHECK(!serial_is_open(&sp));
}

/*
 * A handle that opens but is not a serial port must be refused and closed, not
 * kept. NUL is the shortest thing on Windows that opens and has no line
 * settings; a real one would be a printer port or a file someone mistyped.
 */
static void a_handle_that_is_not_a_port_is_refused_and_released(void)
{
    platform_serial_t s;
    serial_t          sp;
    serial_params_t   p;
    uint8_t           byte = 0;
    size_t            n = 0;

    platform_serial_bind(&s, &sp);
    serial_params_defaults(&p);

    CHECK_EQ(serial_open(&sp, "NUL", &p), RES_EIO);
    CHECK(!serial_is_open(&sp));

    /* And nothing works through it afterwards, which is how we know the
     * handle was let go rather than kept in a half-configured state. */
    CHECK_EQ(serial_write(&sp, &byte, 1, &n), RES_EIO);
    CHECK_EQ(serial_read(&sp, &byte, 1, &n), RES_EIO);
    CHECK_EQ(serial_flush(&sp), RES_EIO);
}

static void a_closed_port_refuses_work_and_closes_idempotently(void)
{
    platform_serial_t s;
    serial_t          sp;
    uint8_t           byte = 0;
    size_t            n = 0;

    platform_serial_bind(&s, &sp);

    CHECK(!serial_is_open(&sp));
    CHECK_EQ(serial_write(&sp, &byte, 1, &n), RES_EIO);
    CHECK_EQ(serial_read(&sp, &byte, 1, &n), RES_EIO);
    CHECK_EQ(serial_flush(&sp), RES_EIO);

    serial_close(&sp);
    serial_close(&sp);
    CHECK(!serial_is_open(&sp));
}

/* ------------------------------------------------- with a real port pair */

static int64_t g_now;

static int64_t fake_now(void)
{
    return g_now;
}

/* Split "COM11,COM12" in place. False when the variable is unset or has no
 * comma, which is not a failure -- only a skip. */
static bool port_pair(char *buf, size_t cap, char **a, char **b)
{
    const char *env = getenv("TSB_TEST_COM_PAIR");
    char       *comma;

    if (!env || env[0] == 0)
        return false;
    if (strlen(env) >= cap)
        return false;

    strcpy(buf, env);
    comma = strchr(buf, ',');
    if (!comma)
        return false;

    *comma = 0;
    *a = buf;
    *b = comma + 1;

    return (*a)[0] != 0 && (*b)[0] != 0;
}

/*
 * The whole exchange over two real ports, with the appliance played by hand at
 * the far end. This is the pty test's counterpart, and it is what actually
 * establishes that the adapter can carry a TaiSEIA transaction on Windows.
 */
static void a_full_exchange_over_a_real_pair(void)
{
    char              names[64];
    char             *near_name;
    char             *far_name;
    platform_serial_t near_s;
    platform_serial_t far_s;
    serial_t          near_sp;
    serial_t          far_sp;
    serial_params_t   p;
    uart_link_t       u;
    link_t            l;
    link_req_t        req;
    link_resp_t       resp;
    uint8_t           seen[TS_FRAME_MAX_LEN];
    size_t            seen_len = 0;
    uint8_t           reply[6];
    size_t            wrote = 0;
    int               spins;

    if (!port_pair(names, sizeof(names), &near_name, &far_name)) {
        printf("skip: TSB_TEST_COM_PAIR unset; "
               "no serial pair to test against\n");
        return;
    }

    platform_serial_bind(&near_s, &near_sp);
    platform_serial_bind(&far_s, &far_sp);
    serial_params_defaults(&p);

    CHECK_EQ(serial_open(&near_sp, near_name, &p), RES_OK);
    CHECK_EQ(serial_open(&far_sp, far_name, &p), RES_OK);
    if (!serial_is_open(&near_sp) || !serial_is_open(&far_sp)) {
        serial_close(&near_sp);
        serial_close(&far_sp);
        return;
    }

    g_now = 0;
    CHECK_EQ(uart_link_init(&u, &near_sp, fake_now), RES_OK);
    uart_link_bind(&u, &l);

    memset(&req, 0, sizeof(req));
    req.id = 1; req.op = LINK_OP_READ; req.sa_id = 0x04; req.service_id = 0x07;
    CHECK_EQ(link_submit(&l, &req), RES_OK);

    /* Drive the request out and collect it at the other end. A real port
     * takes time the fake never did, so this spins rather than assuming. */
    for (spins = 0; spins < 2000 && seen_len < 6u; spins++) {
        size_t got = 0;

        (void)link_poll(&l, &resp);
        if (serial_read(&far_sp, seen + seen_len, sizeof(seen) - seen_len,
                        &got) == RES_OK)
            seen_len += got;
    }

    CHECK_EQ(seen_len, 6);
    if (seen_len >= 6u) {
        CHECK_EQ(seen[1], 0x04);
        CHECK_EQ(seen[2], 0x07);
        CHECK_EQ(seen[5], ts_checksum(seen, 5));
    }

    reply[0] = 0x06;
    reply[1] = 0x04;
    reply[2] = 0x07;
    reply[3] = 0x00;
    reply[4] = 0x2A;
    reply[5] = ts_checksum(reply, 5);
    CHECK_EQ(serial_write(&far_sp, reply, sizeof(reply), &wrote), RES_OK);
    CHECK_EQ(wrote, 6);

    for (spins = 0; spins < 2000; spins++) {
        if (link_poll(&l, &resp) == RES_OK)
            break;
    }

    CHECK_EQ(resp.id, 1);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.service_id, 0x07);
    CHECK_EQ(resp.data_len, 2);
    if (resp.data_len >= 2u)
        CHECK_EQ((resp.data[0] << 8) | resp.data[1], 42);

    serial_close(&near_sp);
    serial_close(&far_sp);
}

void test_serial_win32(void)
{
    bad_parameters_are_refused_before_opening_anything();
    an_absent_port_is_reported_absent();
    a_handle_that_is_not_a_port_is_refused_and_released();
    a_closed_port_refuses_work_and_closes_idempotently();
    a_full_exchange_over_a_real_pair();
}

#endif /* _WIN32 */
