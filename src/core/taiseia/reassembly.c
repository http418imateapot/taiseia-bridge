/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/taiseia/reassembly.h"

#include "core/taiseia/registration.h"

#include <string.h>

void ts_reasm_begin(ts_reasm_t *r, ts_reply_kind_t kind)
{
    if (!r)
        return;

    memset(r, 0, sizeof(*r));
    r->kind = kind;
}

bool ts_reasm_complete(const ts_reasm_t *r)
{
    return r && r->complete;
}

static result_t append(ts_reasm_t *r, const uint8_t *p, size_t n)
{
    if (n == 0)
        return RES_OK;
    if (n > sizeof(r->data) - r->len) {
        /* A reply larger than we can hold is refused rather than truncated:
         * a short capability list, or a short batch of readings, looks
         * entirely plausible and is wrong. */
        return RES_ETOOBIG;
    }

    memcpy(r->data + r->len, p, n);
    r->len += n;

    return RES_OK;
}

result_t ts_reasm_add(ts_reasm_t *r, const uint8_t *pkt, size_t len)
{
    uint16_t frag = 0;
    result_t res;

    if (!r || !pkt)
        return RES_EINVAL;
    if (r->complete)
        return RES_EBUSY;

    res = ts_reply_fragment(pkt, len, r->kind, &frag);
    if (res != RES_OK)
        return res;

    /*
     * Fragments are numbered downwards and zero is the last. A number that
     * does not follow means a piece was lost or two replies have been mixed,
     * and joining them would produce something that parses.
     */
    if (r->started) {
        if (r->last_fragment == 0 || frag != r->last_fragment - 1u)
            return RES_EFORMAT;
    }

    switch (r->kind) {
    case TS_REPLY_MULTIBYTE: {
        ts_mb_frame_t f;

        res = ts_parse_mb_response(pkt, len, &f);
        if (res != RES_OK)
            return res;

        /* The whole sixteen-bit id is kept and compared, so a fragment for
         * 0x0120 can never be joined onto a reply for 0x20. */
        if (!r->started) {
            r->sa_id      = f.sa_id;
            r->service_id = f.service_id;
            r->write      = f.write;
            r->dtype      = f.dtype;
        } else if (f.service_id != r->service_id || f.dtype != r->dtype) {
            /* Two different replies interleaved. */
            return RES_EFORMAT;
        }

        /* Each fragment reports the items it carries, not the total. */
        if (f.count != TS_MB_COUNT_UNSPECIFIED)
            r->count += f.count;

        res = append(r, f.data, f.data_len);
        break;
    }

    case TS_REPLY_REGISTRATION: {
        ts_frame_t f;

        res = ts_parse_response(pkt, len, &f);
        if (res != RES_OK)
            return res;
        if (!f.registration)
            return RES_EFORMAT;

        if (!r->started) {
            r->sa_id = f.sa_id;
            res = append(r, f.data, f.data_len);
            break;
        }

        /*
         * A continuation repeats the fixed header -- it has to, or the
         * fragment number at its fixed offset could not be read -- and what
         * joins up is everything after it. This is inferred, not stated; see
         * the note in reassembly.h.
         */
        if (f.data_len < TS_REG_HEADER_LEN)
            return RES_EFORMAT;

        res = append(r, f.data + TS_REG_HEADER_LEN,
                     f.data_len - TS_REG_HEADER_LEN);
        break;
    }

    case TS_REPLY_GENERAL:
    default: {
        ts_frame_t f;

        res = ts_parse_response(pkt, len, &f);
        if (res != RES_OK)
            return res;

        r->sa_id      = f.sa_id;
        r->service_id = f.service_id;
        r->write      = f.write;
        res = append(r, f.data, f.data_len);
        break;
    }
    }

    if (res != RES_OK)
        return res;

    r->started       = true;
    r->last_fragment = frag;
    r->fragments++;

    if (frag == 0) {
        /*
         * What we hand over is whole, so its header must say so. The fragment
         * number shares a byte with the link speed, and the decoder refuses a
         * payload that still claims to be a piece -- rightly, since a caller
         * that does not reassemble must not accept one.
         */
        if (r->kind == TS_REPLY_REGISTRATION && r->len > 3u)
            r->data[3] = (uint8_t)(r->data[3] & 0x0Fu);

        r->complete = true;
        return RES_OK;
    }

    return RES_EAGAIN;
}
