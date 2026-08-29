/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The UART link adapter against a scriptable serial port and a clock made of
 * numbers. Both deadlines the standard sets are tested by moving the clock,
 * not by sleeping.
 */
#include "tests.h"

#include "adapters/taiseia_uart/uart_link.h"
#include "core/taiseia/frame.h"
#include "ports/appliance_link.h"
#include "ports/serial.h"

/* ------------------------------------------------------------ fake serial */

typedef struct {
    bool     open;
    uint8_t  out[256];
    size_t   out_len;
    size_t   write_limit;      /* bytes accepted per call; 0 is unlimited */
    uint8_t  in[256];
    size_t   in_len;
    size_t   in_pos;
    size_t   read_limit;
    bool     fail_write;
    bool     fail_read;
    unsigned flushes;
} fake_serial_t;

static result_t fk_open(void *self, const char *name,
                        const serial_params_t *p)
{
    fake_serial_t *f = (fake_serial_t *)self;

    (void)name;
    (void)p;
    f->open = true;
    return RES_OK;
}

static void fk_close(void *self)
{
    ((fake_serial_t *)self)->open = false;
}

static result_t fk_write(void *self, const uint8_t *buf, size_t n,
                         size_t *written)
{
    fake_serial_t *f = (fake_serial_t *)self;
    size_t         take = n;

    if (!f->open || f->fail_write)
        return RES_EIO;
    if (f->write_limit > 0 && take > f->write_limit)
        take = f->write_limit;
    if (take > sizeof(f->out) - f->out_len)
        take = sizeof(f->out) - f->out_len;
    if (take == 0)
        return RES_EAGAIN;

    memcpy(f->out + f->out_len, buf, take);
    f->out_len += take;
    *written = take;

    return RES_OK;
}

static result_t fk_read(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    fake_serial_t *f = (fake_serial_t *)self;
    size_t         avail;

    if (!f->open)
        return RES_EIO;
    if (f->fail_read)
        return RES_EIO;

    avail = f->in_len - f->in_pos;
    if (avail == 0)
        return RES_EAGAIN;
    if (avail > cap)
        avail = cap;
    if (f->read_limit > 0 && avail > f->read_limit)
        avail = f->read_limit;

    memcpy(buf, f->in + f->in_pos, avail);
    f->in_pos += avail;
    *got = avail;

    return RES_OK;
}

static result_t fk_flush(void *self)
{
    fake_serial_t *f = (fake_serial_t *)self;

    f->flushes++;
    f->in_pos = f->in_len;      /* whatever was buffered is now gone */
    return RES_OK;
}

static bool fk_is_open(void *self)
{
    return ((fake_serial_t *)self)->open;
}

static const serial_vt_t k_fake_serial_vt = {
    fk_open, fk_close, fk_write, fk_read, fk_flush, fk_is_open
};

static void fake_serial_bind(fake_serial_t *f, serial_t *s)
{
    memset(f, 0, sizeof(*f));
    f->open = true;
    s->vt   = &k_fake_serial_vt;
    s->self = f;
}

static void feed(fake_serial_t *f, const uint8_t *p, size_t n)
{
    CHECK(n <= sizeof(f->in) - f->in_len);
    memcpy(f->in + f->in_len, p, n);
    f->in_len += n;
}

/* ------------------------------------------------------------- fake clock */

static int64_t g_now;

static int64_t fake_now(void)
{
    return g_now;
}

/* ------------------------------------------------------------------ setup */

static void wire(fake_serial_t *f, serial_t *sp, uart_link_t *u, link_t *l)
{
    fake_serial_bind(f, sp);
    g_now = 0;
    CHECK_EQ(uart_link_init(u, sp, fake_now), RES_OK);
    uart_link_bind(u, l);
}

static void req_read(link_req_t *r, uint8_t sa, uint8_t svc)
{
    memset(r, 0, sizeof(*r));
    r->id = 7; r->op = LINK_OP_READ; r->sa_id = sa; r->service_id = svc;
}

/* ------------------------------------------------------------------ tests */

static void a_read_exchange_puts_the_right_bytes_on_the_wire(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[6] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(f.flushes, 1);            /* stale bytes cleared first */

    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    CHECK_EQ(f.out_len, 6);
    CHECK_EQ(f.out[0], 0x06);
    CHECK_EQ(f.out[1], 0x04);
    CHECK_EQ(f.out[2], 0x07);
    CHECK_EQ(f.out[3], 0xFF);
    CHECK_EQ(f.out[4], 0xFF);
    CHECK_EQ(f.out[5], ts_checksum(f.out, 5));

    reply[5] = ts_checksum(reply, 5);
    feed(&f, reply, sizeof(reply));

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.id, 7);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.sa_id, 0x04);
    CHECK_EQ(resp.service_id, 0x07);
    CHECK_EQ(resp.data_len, 2);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 42);

    /* Back to idle, ready for the next question. */
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
}

/* A registration reply has no service byte, and the adapter must hand the
 * payload over from byte 2 rather than byte 3. */
static void a_registration_reply_keeps_its_first_payload_byte(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[7] = { 0x07, 0x00, 0x80, 0x01, 0x00, 0x03, 0x00 };

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 9; req.op = LINK_OP_REGISTER; req.sa_id = 0x04;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    /* Registration asks with SA id and service id both zero. */
    CHECK_EQ(f.out[1], 0x00);
    CHECK_EQ(f.out[2], 0x00);

    reply[6] = ts_checksum(reply, 6);
    feed(&f, reply, sizeof(reply));

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.sa_id, 0x00);
    CHECK_EQ(resp.data_len, 4);
    CHECK_EQ(resp.data[0], 0x80);
    CHECK_EQ(resp.data[3], 0x03);
}

static void a_write_exchange_carries_the_value(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[6];

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 3; req.op = LINK_OP_WRITE; req.sa_id = 0x04;
    req.service_id = 0x03; req.value = 70;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    CHECK_EQ(f.out[2], 0x80 | 0x03);       /* write bit set */
    CHECK_EQ(f.out[3], 0x00);
    CHECK_EQ(f.out[4], 70);

    /* Success is defined as a reply identical to the request. */
    memcpy(reply, f.out, 6);
    feed(&f, reply, sizeof(reply));

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.write, 1);
    CHECK_EQ(resp.data[1], 70);
}

/* The clock starts when the last byte of the request is on the wire, not when
 * the request was handed over. */
static void a_silent_appliance_times_out(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now = TS_RESPONSE_TIMEOUT_MS - 1;
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now = TS_RESPONSE_TIMEOUT_MS;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ETIMEOUT);
    CHECK_EQ(resp.id, 7);
}

/*
 * A gap this long means these bytes are not one packet. Continuing to
 * accumulate would eventually assemble a plausible frame out of two halves,
 * which is worse than reporting nothing.
 */
static void a_gap_between_bytes_invalidates_the_packet(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       half[3] = { 0x06, 0x04, 0x07 };

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now = 10;
    feed(&f, half, sizeof(half));
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now = 10 + TS_INTERBYTE_MAX_MS;      /* exactly at the limit: still ok */
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now = 10 + TS_INTERBYTE_MAX_MS + 1;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EFORMAT);
}

static void a_reply_in_pieces_within_the_gap_is_assembled(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[6] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };
    int           i;

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);
    reply[5] = ts_checksum(reply, 5);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    f.read_limit = 1;
    feed(&f, reply, sizeof(reply));

    /* One byte per poll, each within the inter-byte limit of the last. */
    for (i = 0; i < 5; i++) {
        g_now += 5;
        CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    }
    g_now += 5;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.data[1], 0x2A);
}

static void a_malformed_reply_is_refused(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;

    /* A first byte no packet could have: nothing after it can be trusted to
     * line up either. */
    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    {
        uint8_t bad[1] = { 0x03 };

        feed(&f, bad, 1);
    }
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EFORMAT);

    /* A checksum that does not match. */
    wire(&f, &sp, &u, &l);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    {
        uint8_t reply[6] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };

        reply[5] = (uint8_t)(ts_checksum(reply, 5) ^ 0x01u);
        feed(&f, reply, sizeof(reply));
    }
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EFORMAT);
}

/*
 * A reply that arrived after we gave up on it must not be read as the answer
 * to the next question -- every value after that would be attributed to the
 * wrong service, permanently and with nothing to detect it.
 */
static void a_stale_reply_is_flushed_before_the_next_exchange(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       stale[6] = { 0x06, 0x04, 0x01, 0x00, 0x63, 0x00 };
    uint8_t       fresh[6] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };

    wire(&f, &sp, &u, &l);
    stale[5] = ts_checksum(stale, 5);
    fresh[5] = ts_checksum(fresh, 5);

    /* Something is already sitting in the buffer when we ask. */
    feed(&f, stale, sizeof(stale));

    req_read(&req, 0x04, 0x07);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(f.flushes, 1);

    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    feed(&f, fresh, sizeof(fresh));
    CHECK_EQ(link_poll(&l, &resp), RES_OK);

    /* The answer is to the question we asked, not the one before it. */
    CHECK_EQ(resp.service_id, 0x07);
    CHECK_EQ(resp.data[1], 0x2A);
}

static void a_partial_write_is_resumed(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;

    wire(&f, &sp, &u, &l);
    f.write_limit = 1;
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    /* The write loop drains what the port will take, so one poll is enough
     * even at a byte a time. */
    CHECK_EQ(f.out_len, 6);
    CHECK_EQ(f.out[5], ts_checksum(f.out, 5));
}

static void the_adapter_refuses_what_it_cannot_do(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;

    CHECK_EQ(uart_link_init(NULL, &sp, fake_now), RES_EINVAL);
    CHECK_EQ(uart_link_init(&u, NULL, fake_now), RES_EINVAL);
    /* A clock is required, not optional: without one the deadlines are not
     * deadlines. */
    fake_serial_bind(&f, &sp);
    CHECK_EQ(uart_link_init(&u, &sp, NULL), RES_EINVAL);

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_submit(&l, &req), RES_EBUSY);

    /* An unknown operation is refused rather than guessed at. */
    wire(&f, &sp, &u, &l);
    {
        link_req_t bad;

        memset(&bad, 0, sizeof(bad));
        bad.op = (link_op_t)99;
        CHECK_EQ(link_submit(&l, &bad), RES_EINVAL);
        CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    }

    /* A closed port takes the link with it. */
    wire(&f, &sp, &u, &l);
    CHECK(link_is_up(&l));
    link_close(&l);
    CHECK(!f.open);

    fake_serial_bind(&f, &sp);
    CHECK_EQ(uart_link_init(&u, &sp, fake_now), RES_OK);
    uart_link_bind(&u, &l);
    f.open = false;
    CHECK_EQ(link_submit(&l, &req), RES_EIO);
    CHECK(!link_is_up(&l));
}

static void a_write_failure_ends_the_exchange(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;

    wire(&f, &sp, &u, &l);
    req_read(&req, 0x04, 0x07);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    f.fail_write = true;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EIO);

    wire(&f, &sp, &u, &l);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    f.fail_read = true;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EIO);
}

/* ------------------------------------------------------- multi-byte replies */

/* One multi-byte fragment carrying `count` single-byte items of `fill`. */
static size_t mb_reply(uint8_t *out, uint16_t fragment, uint8_t sa,
                       uint8_t service, uint8_t dtype, uint16_t count,
                       uint8_t fill)
{
    size_t len = TS_MB_OVERHEAD + count;
    size_t i;

    out[0] = (uint8_t)(len - 1u);
    out[1] = sa;
    out[2] = service;
    out[3] = TS_MB_RESERVED;
    out[4] = (uint8_t)((fragment >> 8) & 0xFFu);
    out[5] = (uint8_t)(fragment & 0xFFu);
    out[6] = dtype;
    out[7] = (uint8_t)((count >> 8) & 0xFFu);
    out[8] = (uint8_t)(count & 0xFFu);
    for (i = 0; i < count; i++)
        out[TS_MB_DATA_OFFSET + i] = fill;
    out[len - 1u] = ts_checksum(out, len - 1u);

    return len;
}

static void a_multibyte_read_asks_in_the_wider_layout(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[32];
    size_t        n;

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 11; req.op = LINK_OP_READ; req.sa_id = 0xE0;
    req.service_id = 0x20;
    req.multibyte = true; req.dtype = 0x21; req.value_len = 2;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    CHECK_EQ(f.out_len, TS_MB_OVERHEAD + 2u);
    CHECK_EQ(f.out[0], TS_MB_OVERHEAD + 2u - 1u);
    CHECK_EQ(f.out[2], 0x20);                 /* read: no write bit        */
    CHECK_EQ(f.out[3], TS_MB_RESERVED);
    CHECK_EQ(f.out[6], 0x21);
    CHECK_EQ(f.out[7], 0xFF);                 /* asking, not telling       */
    CHECK_EQ(f.out[8], 0xFF);
    CHECK_EQ(f.out[9], 0xFF);
    CHECK_EQ(f.out[10], 0xFF);

    n = mb_reply(reply, 0, 0xE0, 0x20, 0x21, 2, 0x5A);
    feed(&f, reply, n);

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.id, 11);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.multibyte, 1);
    CHECK_EQ(resp.dtype, 0x21);
    CHECK_EQ(resp.count, 2);
    CHECK_EQ(resp.data_len, 2);
    CHECK_EQ(resp.data[0], 0x5A);
    CHECK_EQ(resp.data[1], 0x5A);
}

/*
 * A long answer arrives as several packets, and each one gets its own 200 ms.
 * The clock is moved past the first deadline between fragments deliberately:
 * a single deadline for the whole reply would cut off a legitimate one.
 */
static void a_fragmented_reply_is_gathered_across_deadlines(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[32];
    size_t        n;

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 12; req.op = LINK_OP_READ; req.sa_id = 0xE0;
    req.service_id = 0x30;
    req.multibyte = true; req.dtype = 0x20; req.value_len = 1;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    n = mb_reply(reply, 2, 0xE0, 0x30, 0x20, 4, 0x11);
    feed(&f, reply, n);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now += TS_RESPONSE_TIMEOUT_MS - 10;
    n = mb_reply(reply, 1, 0xE0, 0x30, 0x20, 4, 0x22);
    feed(&f, reply, n);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    /* Well past the deadline the first fragment set, but not past the one
     * the second fragment renewed. */
    g_now += TS_RESPONSE_TIMEOUT_MS - 10;
    n = mb_reply(reply, 0, 0xE0, 0x30, 0x20, 2, 0x33);
    feed(&f, reply, n);

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.multibyte, 1);
    CHECK_EQ(resp.count, 10);
    CHECK_EQ(resp.data_len, 10);
    CHECK_EQ(resp.data[0], 0x11);
    CHECK_EQ(resp.data[4], 0x22);
    CHECK_EQ(resp.data[8], 0x33);
    CHECK_EQ(resp.data[9], 0x33);
}

/* Silence after a fragment is still a timeout: an unfinished reply must not
 * be handed over as if it were whole. */
static void a_reply_that_stops_mid_stream_times_out(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[32];
    size_t        n;

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 13; req.op = LINK_OP_READ; req.sa_id = 0xE0;
    req.service_id = 0x30;
    req.multibyte = true; req.dtype = 0x20; req.value_len = 1;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    n = mb_reply(reply, 1, 0xE0, 0x30, 0x20, 4, 0x11);
    feed(&f, reply, n);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    g_now += TS_RESPONSE_TIMEOUT_MS + 1;
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.id, 13);
    CHECK_EQ(resp.status, RES_ETIMEOUT);
    CHECK_EQ(resp.data_len, 0);
}

/* A registration reply in two pieces, joined before the model sees it. */
static void a_fragmented_registration_reply_is_joined(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       pkt[64];
    size_t        pos;
    size_t        len;

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 16; req.op = LINK_OP_REGISTER; req.sa_id = 0x04;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    pos = 2;
    pkt[pos++] = 0x01; pkt[pos++] = 0x01; pkt[pos++] = 0x00;
    pkt[pos++] = (uint8_t)((1u << 4) | 0x04u);
    pkt[pos++] = 0x00; pkt[pos++] = 0x04;
    pkt[pos++] = 0xA1; pkt[pos++] = 0xA2;
    len = pos + 1u;
    pkt[0] = (uint8_t)(len - 1u);
    pkt[1] = 0x00;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);
    feed(&f, pkt, len);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    pos = 2;
    pkt[pos++] = 0x01; pkt[pos++] = 0x01; pkt[pos++] = 0x00;
    pkt[pos++] = 0x04;
    pkt[pos++] = 0x00; pkt[pos++] = 0x04;
    pkt[pos++] = 0xB1;
    len = pos + 1u;
    pkt[0] = (uint8_t)(len - 1u);
    pkt[1] = 0x00;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);
    feed(&f, pkt, len);

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.multibyte, 0);
    /* Header once, then both payloads. */
    CHECK_EQ(resp.data_len, 9);
    CHECK_EQ(resp.data[3], 0x04);       /* whole, so no fragment number */
    CHECK_EQ(resp.data[6], 0xA1);
    CHECK_EQ(resp.data[7], 0xA2);
    CHECK_EQ(resp.data[8], 0xB1);
}

/*
 * Both fragments in one read. At 9600 bps an appliance can put the next
 * fragment on the wire before we poll again, so the second one has to survive
 * being buffered behind the first.
 */
static void two_fragments_in_one_read_are_both_taken(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    uint8_t       reply[32];
    size_t        n;

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 15; req.op = LINK_OP_READ; req.sa_id = 0xE0;
    req.service_id = 0x30;
    req.multibyte = true; req.dtype = 0x20; req.value_len = 1;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    n = mb_reply(reply, 1, 0xE0, 0x30, 0x20, 3, 0x77);
    feed(&f, reply, n);
    n = mb_reply(reply, 0, 0xE0, 0x30, 0x20, 3, 0x88);
    feed(&f, reply, n);

    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.count, 6);
    CHECK_EQ(resp.data_len, 6);
    CHECK_EQ(resp.data[0], 0x77);
    CHECK_EQ(resp.data[2], 0x77);
    CHECK_EQ(resp.data[3], 0x88);
    CHECK_EQ(resp.data[5], 0x88);
}

/* A multi-byte write says how many items it carries and hands them over
 * unchanged. */
static void a_multibyte_write_carries_its_items(void)
{
    fake_serial_t f;
    serial_t      sp;
    uart_link_t   u;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    static const uint8_t data[] = { 0x00, 0x00, 0x04, 0xD2 };

    wire(&f, &sp, &u, &l);
    memset(&req, 0, sizeof(req));
    req.id = 14; req.op = LINK_OP_WRITE; req.sa_id = 0xE0;
    req.service_id = 0x40;
    req.multibyte = true; req.dtype = 0x22; req.value_len = 4;
    req.count = 1; req.data = data; req.data_len = sizeof(data);

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);

    CHECK_EQ(f.out_len, TS_MB_OVERHEAD + 4u);
    CHECK_EQ(f.out[2], TS_SVC_WRITE_BIT | 0x40);
    CHECK_EQ(f.out[6], 0x22);
    CHECK_EQ(f.out[7], 0x00);
    CHECK_EQ(f.out[8], 0x01);
    CHECK(memcmp(f.out + TS_MB_DATA_OFFSET, data, sizeof(data)) == 0);
    CHECK_EQ(f.out[f.out_len - 1u],
             ts_checksum(f.out, f.out_len - 1u));
}

void test_uart(void)
{
    a_read_exchange_puts_the_right_bytes_on_the_wire();
    a_registration_reply_keeps_its_first_payload_byte();
    a_write_exchange_carries_the_value();
    a_silent_appliance_times_out();
    a_gap_between_bytes_invalidates_the_packet();
    a_reply_in_pieces_within_the_gap_is_assembled();
    a_malformed_reply_is_refused();
    a_stale_reply_is_flushed_before_the_next_exchange();
    a_partial_write_is_resumed();
    the_adapter_refuses_what_it_cannot_do();
    a_write_failure_ends_the_exchange();
    a_multibyte_read_asks_in_the_wider_layout();
    a_fragmented_reply_is_gathered_across_deadlines();
    a_reply_that_stops_mid_stream_times_out();
    a_fragmented_registration_reply_is_joined();
    two_fragments_in_one_read_are_both_taken();
    a_multibyte_write_carries_its_items();
}
