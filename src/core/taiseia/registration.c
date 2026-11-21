/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/taiseia/registration.h"

#include "core/taiseia/datatype.h"

#include <string.h>

#define CLASS_MULTIBYTE_BIT  0x80u
#define CLASS_MASK           0x0Fu

uint32_t ts_baud_from_code(uint8_t code)
{
    switch (code) {
    case 0: return 1200u;
    case 1: return 2400u;
    case 2: return 4800u;
    case 3: return 9600u;
    case 4: return 19200u;
    case 5: return 38400u;
    case 6: return 57600u;
    default: break;
    }
    return 0u;
}

/*
 * Copy a NUL-terminated string out of the payload.
 *
 * On success *consumed counts the terminator too. A string with no terminator
 * inside the payload is refused: the alternative is to invent one, and every
 * byte after it would then be parsed at the wrong offset.
 */
static result_t take_string(const uint8_t *data, size_t len, size_t pos,
                            char *out, size_t out_cap, size_t *consumed)
{
    size_t i;
    size_t limit;

    if (pos >= len)
        return RES_EFORMAT;

    limit = len - pos;
    if (limit > out_cap)
        limit = out_cap;

    for (i = 0; i < limit; i++) {
        if (data[pos + i] == 0u) {
            memcpy(out, &data[pos], i);
            out[i] = '\0';
            *consumed = i + 1u;
            return RES_OK;
        }
    }
    return RES_EFORMAT;
}

/* Bytes one capability record occupies, or 0 when that cannot be determined. */
static size_t record_len(ts_format_t format, const uint8_t *rec, size_t avail,
                         result_t *why)
{
    const ts_datatype_t *t;

    *why = RES_OK;

    if (format == TS_FORMAT_GENERAL)
        return (avail >= TS_REG_GENERAL_RECORD) ? TS_REG_GENERAL_RECORD : 0u;

    if (avail < TS_REG_MB_RECORD_HEADER)
        return 0u;

    t = ts_datatype(rec[TS_REG_MB_OFF_DTYPE]);
    if (!t) {
        /* Without the type we cannot know the record's width, so the rest of
         * the stream is unreadable too. Refuse the whole reply rather than
         * resynchronise by guessing. */
        *why = RES_EUNSUPPORTED;
        return 0u;
    }
    if (t->support_len == 0u) {
        /* A variable-width support datum -- the string type. The standard does
         * not fix its width, so the same argument applies. */
        *why = RES_EUNSUPPORTED;
        return 0u;
    }
    if (t->support_len > TS_SUPPORT_MAX_BYTES) {
        *why = RES_EFORMAT;
        return 0u;
    }

    return TS_REG_MB_RECORD_HEADER + t->support_len;
}

result_t ts_registration_parse(const uint8_t *data, size_t len,
                               ts_registration_t *out)
{
    size_t   pos;
    size_t   used = 0;
    result_t r;

    if (!data || !out)
        return RES_EINVAL;

    memset(out, 0, sizeof(*out));

    if (len < TS_REG_HEADER_LEN)
        return RES_EFORMAT;

    out->format       = (data[0] & CLASS_MULTIBYTE_BIT) ? TS_FORMAT_MULTIBYTE
                                                        : TS_FORMAT_GENERAL;
    out->device_class = (uint8_t)(data[0] & CLASS_MASK);
    out->proto_major  = data[1];
    out->proto_minor  = data[2];
    out->fragment     = (uint8_t)((data[3] >> 4) & 0x0Fu);
    out->baud_code    = (uint8_t)(data[3] & 0x0Fu);
    out->sa_id        = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);

    /*
     * A fragment is only part of the picture, and decoding one alone would
     * report a truncated capability list as if it were complete. Joining them
     * up happens above this, in core/taiseia/reassembly.c, which hands over a
     * payload whose fragment number is zero because it is whole.
     */
    if (out->fragment != 0u)
        return RES_EUNSUPPORTED;

    pos = TS_REG_HEADER_LEN;

    r = take_string(data, len, pos, out->make, sizeof(out->make) - 1u, &used);
    if (r != RES_OK)
        return r;
    pos += used;

    r = take_string(data, len, pos, out->model, sizeof(out->model) - 1u, &used);
    if (r != RES_OK)
        return r;
    pos += used;

    while (pos < len) {
        size_t   n;
        result_t why = RES_OK;
        ts_capability_t *cap;

        n = record_len(out->format, &data[pos], len - pos, &why);
        if (n == 0u)
            return (why != RES_OK) ? why : RES_EFORMAT;
        if (pos + n > len)
            return RES_EFORMAT;   /* the stream does not divide evenly */

        if (out->n_caps >= TS_MAX_CAPABILITIES) {
            /* Refuse rather than truncate: a short capability list looks
             * plausible and is wrong, and a device is then reported as unable
             * to do things it can do. */
            return RES_EFORMAT;
        }

        cap = &out->caps[out->n_caps];
        cap->writable    = (data[pos] & TS_SVC_WRITE_BIT) != 0;

        if (out->format == TS_FORMAT_GENERAL) {
            cap->service_id  = (uint8_t)(data[pos] & TS_SVC_ID_MASK);
            cap->dtype_known = false;
            cap->dtype       = 0;
            cap->support[0]  = data[pos + 1u];
            cap->support[1]  = data[pos + 2u];
            cap->support_len = 2u;
        } else {
            uint16_t id = (uint16_t)(((uint16_t)data[pos + TS_REG_MB_OFF_ID] << 8) |
                                     data[pos + TS_REG_MB_OFF_ID + 1u]);

            /* The record has room for sixteen bits of service id; everything
             * above this layer holds seven. Masking would quietly turn one
             * service into another, so refuse instead. Bits 6..0 of the
             * service type byte are reserved and are not read. */
            if (id > TS_SVC_ID_MAX)
                return RES_EUNSUPPORTED;

            cap->service_id  = (uint8_t)id;
            cap->dtype_known = true;
            cap->dtype       = data[pos + TS_REG_MB_OFF_DTYPE];
            cap->support_len = n - TS_REG_MB_RECORD_HEADER;
            memcpy(cap->support, &data[pos + TS_REG_MB_RECORD_HEADER],
                   cap->support_len);
        }

        out->n_caps++;
        pos += n;
    }

    return RES_OK;
}

const ts_capability_t *ts_registration_find(const ts_registration_t *reg,
                                            uint8_t service_id)
{
    size_t i;

    if (!reg)
        return NULL;

    service_id = (uint8_t)(service_id & TS_SVC_ID_MASK);

    for (i = 0; i < reg->n_caps; i++) {
        if (reg->caps[i].service_id == service_id)
            return &reg->caps[i];
    }
    return NULL;
}
