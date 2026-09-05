/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Structured to mirror the Python oracle modules, not the production codec.
 */
#include "reference.h"

#include "core/taiseia/datatype.h"

/* sigma_w from lab/oracle/readings.py. */
static int32_t ref_sigma(uint32_t x, unsigned bits, int is_signed)
{
    if (is_signed && x >= ((uint32_t)1u << (bits - 1u)))
        return (int32_t)x - (int32_t)((uint32_t)1u << bits);

    return (int32_t)x;
}

int ref_carryable(uint8_t dtype, unsigned *width, int *is_signed)
{
    const ts_datatype_t *t = ts_datatype(dtype);

    if (t == 0)
        return 0;
    if (t->value_len != 1u && t->value_len != 2u)
        return 0;

    *width     = (unsigned)t->value_len;
    *is_signed = t->is_signed ? 1 : 0;
    return 1;
}

void ref_domain(unsigned width, int is_signed, int32_t *lo, int32_t *hi)
{
    unsigned bits = width * 8u;

    if (is_signed) {
        *lo = -(int32_t)((uint32_t)1u << (bits - 1u));
        *hi = (int32_t)((uint32_t)1u << (bits - 1u)) - 1;
    } else {
        *lo = 0;
        *hi = (int32_t)(((uint32_t)1u << bits) - 1u);
    }
}

int ref_encode(uint8_t dtype, int layout, int32_t v, uint8_t *out)
{
    unsigned width;
    int      is_signed;
    int32_t  lo;
    int32_t  hi;
    uint32_t n;
    int32_t  modulus;
    int32_t  residue;

    if (!ref_carryable(dtype, &width, &is_signed))
        return 0;

    ref_domain(width, is_signed, &lo, &hi);
    if (v < lo || v > hi)
        return 0;

    /* enc_A(v) = v mod 2^16;  enc_B(v) = v mod 2^8, and mod 2^16 at width 2. */
    modulus = (layout == REF_LAYOUT_B && width == 1u) ? 256 : 65536;
    residue = v % modulus;
    if (residue < 0)
        residue += modulus;
    n = (uint32_t)residue;

    out[0] = (uint8_t)(n / 256u);
    out[1] = (uint8_t)(n % 256u);
    return 1;
}

int ref_decode(uint8_t dtype, int reading, const uint8_t *in, int32_t *out)
{
    unsigned width;
    int      is_signed;
    int32_t  lo;
    int32_t  hi;
    uint32_t field;
    uint32_t pad;
    int32_t  v;

    if (!ref_carryable(dtype, &width, &is_signed))
        return 0;

    ref_domain(width, is_signed, &lo, &hi);
    field = 256u * (uint32_t)in[0] + (uint32_t)in[1];

    /* dec_wide(b) = sigma_16(field) if in V_t, else refuse. At width 2 there is
     * no padding and every reading collapses onto this one. */
    if (reading == REF_READ_WIDE || width == 2u) {
        v = ref_sigma(field, 16u, is_signed);
        if (v < lo || v > hi)
            return 0;
        *out = v;
        return 1;
    }

    pad = (uint32_t)in[0];
    v   = ref_sigma((uint32_t)in[1], 8u, is_signed);

    if (reading == REF_READ_STRICT && pad != 0u)
        return 0;
    if (reading == REF_READ_LENIENT && !(pad == 0u || (pad == 255u && v < 0)))
        return 0;

    if (v < lo || v > hi)
        return 0;

    *out = v;
    return 1;
}
