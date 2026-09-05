/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/taiseia/general.h"

#include "core/taiseia/datatype.h"

#include <stdbool.h>

/*
 * Read a `bits`-wide unsigned number as two's complement, when the type is
 * signed. Unsigned types pass through.
 *
 * Written as a subtraction rather than as a cast to int8_t or int16_t. The cast
 * is the obvious way to write this and it is implementation-defined in C99
 * (6.3.1.3) whenever the value will not fit the signed type -- which is exactly
 * the case this function exists to handle. gcc and MSVC both happen to do what
 * is wanted, so a test on either compiler passes and establishes nothing about
 * the code. lab/proofs/naive.c keeps the obvious version so the proof can be
 * shown catching what the tests cannot.
 */
static int32_t sigma(uint32_t x, unsigned bits, bool is_signed)
{
    uint32_t half = (uint32_t)1u << (bits - 1u);

    if (is_signed && x >= half)
        return (int32_t)x - (int32_t)(half * 2u);

    return (int32_t)x;
}

static bool carryable(const ts_datatype_t *t)
{
    return t != NULL && t->value_len >= 1u &&
           t->value_len <= (size_t)TS_GENERAL_DATA_BYTES;
}

result_t ts_general_domain(uint8_t dtype, int32_t *lo, int32_t *hi)
{
    const ts_datatype_t *t = ts_datatype(dtype);
    uint32_t             span;

    if (!lo || !hi)
        return RES_EINVAL;
    if (!carryable(t))
        return RES_EUNSUPPORTED;

    /* 2^bits. The shift is on an unsigned value and `carryable` has bounded
     * the width at two bytes, so the count is 8 or 16 and never the width of
     * the type being shifted. */
    span = (uint32_t)1u << (t->value_len * 8u);

    if (t->is_signed) {
        *lo = -(int32_t)(span / 2u);
        *hi = (int32_t)(span / 2u) - 1;
    } else {
        *lo = 0;
        *hi = (int32_t)(span - 1u);
    }

    return RES_OK;
}

result_t ts_general_encode(uint8_t dtype, ts_layout_t layout, int32_t value,
                           uint8_t *out, size_t cap)
{
    const ts_datatype_t *t = ts_datatype(dtype);
    int32_t              lo = 0;
    int32_t              hi = 0;
    uint32_t             n;
    result_t             r;

    if (!out || cap < (size_t)TS_GENERAL_DATA_BYTES)
        return RES_EINVAL;
    if (layout != TS_LAYOUT_SIGN_EXTEND && layout != TS_LAYOUT_ZERO_FILL)
        return RES_EINVAL;

    r = ts_general_domain(dtype, &lo, &hi);
    if (r != RES_OK)
        return r;
    if (value < lo || value > hi)
        return RES_EINVAL;

    /*
     * Equation (2). The domain check bounds value to [-32768, 65535].
     * Form a nonnegative residue before converting, so the conversion preserves
     * its value as well as being C99-defined. A direct negative-to-unsigned cast
     * is defined modular arithmetic, but fails CBMC's stronger conversion check.
     * Mask only unsigned values; no signed representation is assumed.
     */
    n = (uint32_t)(value < 0 ? value + 65536 : value);

    if (layout == TS_LAYOUT_ZERO_FILL && t->value_len == 1u)
        n &= 0xFFu;
    else
        n &= 0xFFFFu;

    out[0] = (uint8_t)((n >> 8) & 0xFFu);
    out[1] = (uint8_t)(n & 0xFFu);

    return RES_OK;
}

result_t ts_general_decode(uint8_t dtype, ts_reading_t reading,
                           const uint8_t *in, size_t len, int32_t *out)
{
    const ts_datatype_t *t = ts_datatype(dtype);
    int32_t              lo = 0;
    int32_t              hi = 0;
    uint32_t             field;
    uint32_t             pad;
    int32_t              v;
    result_t             r;

    if (!in || !out || len < (size_t)TS_GENERAL_DATA_BYTES)
        return RES_EINVAL;
    if (reading != TS_READ_WIDE && reading != TS_READ_LOW &&
        reading != TS_READ_STRICT && reading != TS_READ_LENIENT)
        return RES_EINVAL;

    r = ts_general_domain(dtype, &lo, &hi);
    if (r != RES_OK)
        return r;

    field = ((uint32_t)in[0] << 8) | (uint32_t)in[1];

    /*
     * Equation (3) for the whole field. A two-byte value fills it, so there is
     * nothing above the value and every reading is "read the field" -- which is
     * why the eight two-byte types have no interesting cells.
     */
    if (reading == TS_READ_WIDE ||
        t->value_len == (size_t)TS_GENERAL_DATA_BYTES) {
        v = sigma(field, 16u, t->is_signed);
        if (v < lo || v > hi)
            return RES_EFORMAT;
        *out = v;
        return RES_OK;
    }

    /* One byte wide: the value is the low byte and `pad` is what sits above it.
     * Equation (4) is entirely a statement about `pad`. */
    pad = (uint32_t)in[0];
    v   = sigma((uint32_t)in[1], 8u, t->is_signed);

    switch (reading) {
    case TS_READ_LOW:
        break;

    case TS_READ_STRICT:
        if (pad != 0x00u)
            return RES_EFORMAT;
        break;

    case TS_READ_LENIENT:
        /* Both layouts and nothing else: 00 from the zero-filling sender, FF
         * from the sign-extending one, and FF only where it could have come
         * from a negative value. */
        if (!(pad == 0x00u || (pad == 0xFFu && v < 0)))
            return RES_EFORMAT;
        break;

    default:
        return RES_EINVAL;
    }

    if (v < lo || v > hi)
        return RES_EFORMAT;

    *out = v;
    return RES_OK;
}
