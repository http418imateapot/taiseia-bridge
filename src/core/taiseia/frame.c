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

/* Shared validation. `payload_at` is 2 for a registration reply, 3 otherwise;
 * the caller has already decided which, because the bytes cannot say. */
static result_t parse_common(const uint8_t *buf, size_t len, ts_frame_t *out,
                             bool registration)
{
    size_t   declared;
    result_t r;
    uint8_t  expect;
    size_t   header;

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

    out->sa_id        = buf[1];
    out->registration = registration;

    /* length(1) + SA id(1) + checksum(1), plus a service byte unless this is
     * a registration reply. */
    header = registration ? TS_REG_FRAME_OVERHEAD : TS_FRAME_OVERHEAD;

    if (!registration) {
        out->service_id = (uint8_t)(buf[2] & TS_SVC_ID_MASK);
        out->write      = (buf[2] & TS_SVC_WRITE_BIT) != 0;
        out->data       = &buf[3];
    } else {
        out->data = &buf[2];
    }
    out->data_len = len - header;

    return RES_OK;
}

result_t ts_parse_request(const uint8_t *buf, size_t len, ts_frame_t *out)
{
    return parse_common(buf, len, out, false);
}

result_t ts_parse_response(const uint8_t *buf, size_t len, ts_frame_t *out)
{
    bool registration;

    /* buf[1] is only safe to read once the length checks have run, so peek
     * behind the same guards parse_common uses. */
    if (!buf || !out)
        return RES_EINVAL;
    if (len < TS_FRAME_MIN_LEN)
        return (len == 0) ? RES_EAGAIN : RES_EFORMAT;

    registration = (buf[1] == TS_SA_ID_REGISTRATION);

    return parse_common(buf, len, out, registration);
}

/* ------------------------------------------------- multi-byte data format --
 *
 * The header is fixed at TS_MB_DATA_OFFSET bytes and the data field follows
 * it, so the whole packet is TS_MB_OVERHEAD plus however much data there is.
 * The offsets are named in limits.h and nowhere else.
 */
static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static result_t build_mb(uint8_t *out, size_t cap, size_t *out_len,
                         uint8_t sa_id, bool write, uint16_t service_id,
                         uint8_t dtype, uint16_t count, const uint8_t *data,
                         size_t data_len, uint8_t fill)
{
    size_t len = TS_MB_OVERHEAD + data_len;

    if (!out || !out_len)
        return RES_EINVAL;
    if (len > TS_FRAME_MAX_LEN)
        return RES_ETOOBIG;
    if (cap < len)
        return RES_ETOOBIG;

    out[0] = (uint8_t)len;
    out[1] = sa_id;
    /* Bits 6..0 of the service type are reserved and written as zero. */
    out[TS_MB_OFF_SERVICE_TYPE] = (uint8_t)(write ? TS_SVC_WRITE_BIT : 0u);
    put_be16(&out[TS_MB_OFF_SERVICE_ID], service_id);
    put_be16(&out[TS_MB_OFF_RESERVED], (uint16_t)TS_MB_RESERVED);
    put_be16(&out[TS_MB_OFF_FRAGMENT], 0u);   /* we never send a fragment */
    out[TS_MB_OFF_DTYPE] = dtype;
    put_be16(&out[TS_MB_OFF_COUNT], count);

    if (data)
        memcpy(&out[TS_MB_DATA_OFFSET], data, data_len);
    else
        memset(&out[TS_MB_DATA_OFFSET], fill, data_len);

    out[len - 1u] = ts_checksum(out, len - 1u);

    *out_len = len;
    return RES_OK;
}

result_t ts_build_mb_read(uint8_t *out, size_t cap, size_t *out_len,
                          uint8_t sa_id, uint16_t service_id,
                          uint8_t dtype, size_t value_len)
{
    /* A read still carries a data field, one item wide, every byte 0xFF. A
     * zero-width type has no such field to describe and cannot be asked for
     * this way. */
    if (value_len == 0 || value_len > TS_DATA_MAX)
        return RES_EINVAL;

    return build_mb(out, cap, out_len, sa_id, false, service_id, dtype,
                    (uint16_t)TS_MB_COUNT_UNSPECIFIED, NULL, value_len,
                    TS_READ_FILL);
}

result_t ts_build_mb_write(uint8_t *out, size_t cap, size_t *out_len,
                           uint8_t sa_id, uint16_t service_id,
                           uint8_t dtype, uint16_t count,
                           const uint8_t *data, size_t data_len)
{
    if (!data || data_len == 0 || data_len > TS_DATA_MAX)
        return RES_EINVAL;

    return build_mb(out, cap, out_len, sa_id, true, service_id, dtype, count,
                    data, data_len, 0);
}

result_t ts_parse_mb_response(const uint8_t *buf, size_t len,
                              ts_mb_frame_t *out)
{
    size_t   declared;
    result_t r;

    if (!buf || !out)
        return RES_EINVAL;

    memset(out, 0, sizeof(*out));

    r = ts_frame_declared_len(buf, len, &declared);
    if (r != RES_OK)
        return r;
    if (declared != len)
        return RES_EFORMAT;

    /* Shorter than the header cannot be this format, whatever it is. */
    if (len < TS_MB_OVERHEAD)
        return RES_EFORMAT;

    if (ts_checksum(buf, len - 1u) != buf[len - 1u])
        return RES_EFORMAT;

    out->sa_id        = buf[1];
    out->service_type = buf[TS_MB_OFF_SERVICE_TYPE];
    out->write        = (buf[TS_MB_OFF_SERVICE_TYPE] & TS_SVC_WRITE_BIT) != 0;
    out->service_id   = get_be16(&buf[TS_MB_OFF_SERVICE_ID]);
    out->reserved     = get_be16(&buf[TS_MB_OFF_RESERVED]);
    out->fragment     = get_be16(&buf[TS_MB_OFF_FRAGMENT]);
    out->dtype        = buf[TS_MB_OFF_DTYPE];
    out->count        = get_be16(&buf[TS_MB_OFF_COUNT]);
    out->data         = &buf[TS_MB_DATA_OFFSET];
    out->data_len   = len - TS_MB_OVERHEAD;

    return RES_OK;
}

result_t ts_reply_fragment(const uint8_t *buf, size_t len,
                           ts_reply_kind_t kind, uint16_t *fragment)
{
    if (!buf || !fragment)
        return RES_EINVAL;

    switch (kind) {
    case TS_REPLY_MULTIBYTE:
        if (len < TS_MB_OVERHEAD)
            return RES_EFORMAT;
        *fragment = get_be16(&buf[TS_MB_OFF_FRAGMENT]);
        return RES_OK;

    case TS_REPLY_REGISTRATION:
        /* Payload byte 3 shares its byte with the link speed: the fragment
         * number is the high nibble. The payload starts at byte 2, so that is
         * byte 5 of the packet. */
        if (len < 6u)
            return RES_EFORMAT;
        *fragment = (uint16_t)((buf[5] >> 4) & 0x0Fu);
        return RES_OK;

    case TS_REPLY_GENERAL:
    default:
        /* A general reply is six bytes and has nowhere to put one. */
        *fragment = 0;
        return RES_OK;
    }
}
