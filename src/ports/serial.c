/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dispatch for the serial port, so an unbound port or an adapter that leaves
 * an entry point unset fails as a typed result at the call site.
 */
#include "ports/serial.h"

void serial_params_defaults(serial_params_t *p)
{
    if (!p)
        return;

    p->baud      = 9600;
    p->data_bits = 8;
    p->parity    = 'N';
    p->stop_bits = 1;
}

result_t serial_open(serial_t *s, const char *name, const serial_params_t *p)
{
    if (!s || !s->vt || !s->vt->open || !name || !p)
        return RES_EINVAL;

    return s->vt->open(s->self, name, p);
}

void serial_close(serial_t *s)
{
    if (!s || !s->vt || !s->vt->close)
        return;

    s->vt->close(s->self);
}

result_t serial_write(serial_t *s, const uint8_t *buf, size_t n,
                      size_t *written)
{
    if (!s || !s->vt || !s->vt->write || !buf || !written)
        return RES_EINVAL;

    *written = 0;
    if (n == 0)
        return RES_OK;

    return s->vt->write(s->self, buf, n, written);
}

result_t serial_read(serial_t *s, uint8_t *buf, size_t cap, size_t *got)
{
    if (!s || !s->vt || !s->vt->read || !buf || !got)
        return RES_EINVAL;

    *got = 0;
    if (cap == 0)
        return RES_EAGAIN;

    return s->vt->read(s->self, buf, cap, got);
}

result_t serial_flush(serial_t *s)
{
    if (!s || !s->vt || !s->vt->flush)
        return RES_EINVAL;

    return s->vt->flush(s->self);
}

bool serial_is_open(serial_t *s)
{
    if (!s || !s->vt || !s->vt->is_open)
        return false;

    return s->vt->is_open(s->self);
}
