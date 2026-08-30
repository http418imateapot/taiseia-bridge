/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The two propositions, proved over arbitrary input instead of enumerated.
 *
 * lab/oracle/sweep.py establishes these by walking every value of every domain,
 * which is complete but is a statement about the Python. This establishes them
 * about the C, for any type code and any value, without listing either.
 *
 * P2: a lenient receiver recovers every value, under both layouts.
 * P1: the two cells that misread do so on exactly the negative values of a
 *     signed one-byte type -- stated generally, rather than for INT8 alone.
 *
 * ON ASSUME VERSUS ASSERT. Everything a conforming sender is entitled to do is
 * assumed; everything the code is obliged to do is asserted. In particular the
 * success of `ts_general_encode` on an in-domain value is ASSERTED, not
 * assumed. Assuming it would prune exactly the paths where the encoder wrongly
 * refuses a valid value, and the round-trip below would then hold vacuously --
 * which is the failure this directory keeps finding in its own work.
 */
#include "core/taiseia/general.h"

#include "reference.h"

uint8_t nondet_u8(void);
int32_t nondet_i32(void);

int main(void)
{
    uint8_t  dtype     = nondet_u8();
    uint8_t  layout    = nondet_u8();
    int32_t  v         = nondet_i32();
    uint8_t  b[TS_GENERAL_DATA_BYTES];
    int32_t  back      = 0;
    int32_t  lo        = 0;
    int32_t  hi        = 0;
    unsigned width     = 0;
    int      is_signed = 0;
    int      narrow_negative;
    int      recovered;
    result_t rc;

    __CPROVER_assume(layout <= (uint8_t)TS_LAYOUT_ZERO_FILL);

    /* A type the format can carry, holding a value it can hold: this is a claim
     * about what a conforming sender emits, and nothing else. */
    rc = ts_general_domain(dtype, &lo, &hi);
    __CPROVER_assume(rc == RES_OK);
    __CPROVER_assume(v >= lo && v <= hi);

    rc = ts_general_encode(dtype, (ts_layout_t)layout, v, b, sizeof b);
    __CPROVER_assert(rc == RES_OK,
                     "an in-domain value is always encodable");

    /* P2 -- the reason lenient is the recommended default for a receiver. */
    rc        = ts_general_decode(dtype, TS_READ_LENIENT, b, sizeof b, &back);
    recovered = (rc == RES_OK) && (back == v);
    __CPROVER_assert(recovered,
                     "a lenient receiver recovers every value under either"
                     " layout");

    (void)ref_carryable(dtype, &width, &is_signed);
    narrow_negative = (width == 1u) && is_signed && (v < 0);

    /* P1 -- and the two cells that fail, fail on nothing else. */
    if (layout == (uint8_t)TS_LAYOUT_SIGN_EXTEND) {
        rc        = ts_general_decode(dtype, TS_READ_STRICT, b, sizeof b,
                                      &back);
        recovered = (rc == RES_OK) && (back == v);
        __CPROVER_assert(recovered == !narrow_negative,
                         "sign-extended bytes survive a strict reading exactly"
                         " when the value is not a negative one-byte value");
    } else {
        rc        = ts_general_decode(dtype, TS_READ_WIDE, b, sizeof b, &back);
        recovered = (rc == RES_OK) && (back == v);
        __CPROVER_assert(recovered == !narrow_negative,
                         "zero-filled bytes survive a wide reading exactly when"
                         " the value is not a negative one-byte value");
    }

    return 0;
}
