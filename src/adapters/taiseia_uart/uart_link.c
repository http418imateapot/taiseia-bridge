/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "adapters/taiseia_uart/uart_link.h"

#include "core/taiseia/frame.h"

#include <string.h>

result_t uart_link_init(uart_link_t *u, serial_t *sp, int64_t (*now_ms)(void))
{
    if (!u || !sp || !now_ms)
        return RES_EINVAL;

    memset(u, 0, sizeof(*u));
    u->sp     = sp;
    u->now_ms = now_ms;
    u->phase  = UL_IDLE;

    return RES_OK;
}

static void finish(uart_link_t *u, result_t status)
{
    memset(&u->result, 0, sizeof(u->result));
    u->result.id     = u->req_id;
    u->result.status = status;
    u->phase         = UL_DONE;
}

static void finish_ok(uart_link_t *u, const ts_frame_t *f)
{
    memset(&u->result, 0, sizeof(u->result));
    u->result.id         = u->req_id;
    u->result.status     = RES_OK;
    u->result.sa_id      = f->sa_id;
    u->result.service_id = f->service_id;
    u->result.write      = f->write;
    u->result.data_len   = f->data_len;

    if (f->data_len > sizeof(u->result.data)) {
        u->result.status   = RES_ETOOBIG;
        u->result.data_len = 0;
    } else if (f->data_len > 0) {
        memcpy(u->result.data, f->data, f->data_len);
    }

    u->phase = UL_DONE;
}

static result_t ul_submit(void *self, const link_req_t *req)
{
    uart_link_t *u = (uart_link_t *)self;
    result_t     r;

    if (!u || !req)
        return RES_EINVAL;
    if (!serial_is_open(u->sp))
        return RES_EIO;
    if (u->phase != UL_IDLE)
        return RES_EBUSY;

    switch (req->op) {
    case LINK_OP_READ:
        r = ts_build_read(u->req, sizeof(u->req), &u->req_len,
                          req->sa_id, req->service_id);
        break;
    case LINK_OP_REGISTER:
        r = ts_build_read(u->req, sizeof(u->req), &u->req_len,
                          (uint8_t)TS_SA_ID_REGISTRATION, 0u);
        break;
    case LINK_OP_WRITE:
        r = ts_build_write(u->req, sizeof(u->req), &u->req_len,
                           req->sa_id, req->service_id, req->value);
        break;
    default:
        return RES_EINVAL;
    }
    if (r != RES_OK)
        return r;

    /* Anything still in the buffer belongs to an exchange we have already
     * given up on. Reading it as this reply would attribute one service's
     * value to another, permanently and with nothing to detect it. */
    (void)serial_flush(u->sp);

    u->req_id   = req->id;
    u->req_sent = 0;
    u->rx_len   = 0;
    u->phase    = UL_WRITING;

    return RES_OK;
}

static void step_writing(uart_link_t *u)
{
    while (u->req_sent < u->req_len) {
        size_t   wrote = 0;
        result_t r = serial_write(u->sp, u->req + u->req_sent,
                                  u->req_len - u->req_sent, &wrote);

        if (r == RES_EAGAIN || (r == RES_OK && wrote == 0))
            return;                       /* try again next poll */
        if (r != RES_OK) {
            finish(u, RES_EIO);
            return;
        }
        u->req_sent += wrote;
    }

    /* The clock starts when the last byte is on the wire, not when the
     * request was handed to us. */
    {
        int64_t now = u->now_ms();

        u->deadline_ms  = now + TS_RESPONSE_TIMEOUT_MS;
        u->last_byte_ms = now;
        u->phase        = UL_READING;
    }
}

static void step_reading(uart_link_t *u)
{
    int64_t  now = u->now_ms();
    size_t   got = 0;
    size_t   need = 0;
    result_t r;

    if (u->rx_len < sizeof(u->rx)) {
        r = serial_read(u->sp, u->rx + u->rx_len, sizeof(u->rx) - u->rx_len,
                        &got);
        if (r == RES_OK && got > 0) {
            u->rx_len += got;
            u->last_byte_ms = now;
        } else if (r != RES_OK && r != RES_EAGAIN) {
            finish(u, RES_EIO);
            return;
        }
    }

    if (u->rx_len == 0) {
        if (now >= u->deadline_ms)
            finish(u, RES_ETIMEOUT);
        return;
    }

    r = ts_frame_declared_len(u->rx, u->rx_len, &need);
    if (r == RES_EFORMAT) {
        /* The first byte is not a length any packet could have, so nothing
         * that follows can be trusted to line up either. */
        finish(u, RES_EFORMAT);
        return;
    }

    if (r == RES_OK && u->rx_len >= need) {
        ts_frame_t f;

        r = ts_parse_response(u->rx, need, &f);
        if (r != RES_OK) {
            finish(u, r);
            return;
        }
        finish_ok(u, &f);
        return;
    }

    /* Still incomplete. A gap this long means these bytes are not one packet,
     * and continuing to accumulate would eventually assemble a plausible frame
     * out of two halves. */
    if (now - u->last_byte_ms > TS_INTERBYTE_MAX_MS) {
        finish(u, RES_EFORMAT);
        return;
    }

    if (now >= u->deadline_ms)
        finish(u, RES_ETIMEOUT);
}

static result_t ul_poll(void *self, link_resp_t *out)
{
    uart_link_t *u = (uart_link_t *)self;

    if (!u || !out)
        return RES_EINVAL;

    if (u->phase == UL_WRITING)
        step_writing(u);
    if (u->phase == UL_READING)
        step_reading(u);

    if (u->phase != UL_DONE)
        return RES_EAGAIN;

    *out     = u->result;
    u->phase = UL_IDLE;

    return RES_OK;
}

static bool ul_is_up(void *self)
{
    uart_link_t *u = (uart_link_t *)self;

    return u && serial_is_open(u->sp);
}

static void ul_close(void *self)
{
    uart_link_t *u = (uart_link_t *)self;

    if (!u)
        return;

    serial_close(u->sp);
    u->phase  = UL_IDLE;
    u->rx_len = 0;
}

static const link_vt_t k_uart_vt = {
    ul_submit, ul_poll, ul_is_up, ul_close
};

void uart_link_bind(uart_link_t *u, link_t *out)
{
    if (!out)
        return;

    out->vt   = &k_uart_vt;
    out->self = u;
}
