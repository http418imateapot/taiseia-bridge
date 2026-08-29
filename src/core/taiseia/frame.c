/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/taiseia/frame.h"

#include <string.h>

uint8_t ts_checksum(const uint8_t *buf, size_t n)
{
    uint8_t x = 0;
    size_t  i;

    if (!buf)
        return 0;

    for (i = 0; i < n; i++)
        x = (uint8_t)(x ^ buf[i]);

    return x;
}

/* Both request builders differ only in the service byte and the payload, so
 * the framing lives here once. */
static result_t build_general(uint8_t *out, size_t cap, size_t *out_len,
                              uint8_t sa_id, uint8_t service_byte,
                              uint8_t hi, uint8_t lo)
{
    if (!out || !out_len)
        return RES_EINVAL;
    if (cap < TS_GENERAL_REQ_LEN)
        return RES_ETOOBIG;

    out[0] = (uint8_t)TS_GENERAL_REQ_LEN;
    out[1] = sa_id;
    out[2] = service_byte;
    out[3] = hi;
    out[4] = lo;
    out[5] = ts_checksum(out, TS_GENERAL_REQ_LEN - 1u);

    *out_len = TS_GENERAL_REQ_LEN;
    return RES_OK;
}

result_t ts_build_read(uint8_t *out, size_t cap, size_t *out_len,
                       uint8_t sa_id, uint8_t service_id)
{
    if (service_id > TS_SVC_ID_MAX)
        return RES_EINVAL;

    return build_general(out, cap, out_len, sa_id,
                         (uint8_t)(service_id & TS_SVC_ID_MASK),
                         TS_READ_FILL, TS_READ_FILL);
}

result_t ts_build_write(uint8_t *out, size_t cap, size_t *out_len,
                        uint8_t sa_id, uint8_t service_id, uint16_t value)
{
    if (service_id > TS_SVC_ID_MAX)
        return RES_EINVAL;

    return build_general(out, cap, out_len, sa_id,
                         (uint8_t)((service_id & TS_SVC_ID_MASK) |
                                   TS_SVC_WRITE_BIT),
                         (uint8_t)((value >> 8) & 0xFFu),
                         (uint8_t)(value & 0xFFu));
}

result_t ts_frame_declared_len(const uint8_t *buf, size_t have, size_t *need)
{
    size_t declared;

    if (!buf || !need)
        return RES_EINVAL;
    if (have == 0)
        return RES_EAGAIN;

    declared = buf[0];

    /* Lengths 0..5 and 255 are reserved. Treating a reserved value as a real
     * length is how a desynchronised stream turns into a read of arbitrary
     * size, so it is rejected here rather than clamped. */
    if (declared < TS_FRAME_MIN_LEN || declared > TS_FRAME_MAX_LEN)
        return RES_EFORMAT;

    *need = declared;
    return RES_OK;
}

result_t ts_parse(const uint8_t *buf, size_t len, ts_frame_t *out)
{
    size_t   declared;
    result_t r;
    uint8_t  expect;

    if (!buf || !out)
        return RES_EINVAL;

    memset(out, 0, sizeof(*out));

    r = ts_frame_declared_len(buf, len, &declared);
    if (r != RES_OK)
        return r;

    /* The declared length must match what we actually hold. Accepting a packet
     * that claims to be longer than the bytes in hand would read past the
     * buffer; accepting one that claims to be shorter would leave trailing
     * bytes unaccounted for. */
    if (declared != len)
        return RES_EFORMAT;

    expect = ts_checksum(buf, len - 1u);
    if (expect != buf[len - 1u])
        return RES_EFORMAT;

    out->sa_id      = buf[1];
    out->service_id = (uint8_t)(buf[2] & TS_SVC_ID_MASK);
    out->write      = (buf[2] & TS_SVC_WRITE_BIT) != 0;
    out->data       = &buf[3];
    out->data_len   = len - TS_FRAME_OVERHEAD;

    return RES_OK;
}
