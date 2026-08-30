/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Does the decoder agree with the equations, for EVERY input?
 *
 * The unit test in tests/test_general.c walks the same cells, but it walks the
 * ones somebody listed, on one compiler, in one build. Here the type code, the
 * reading and both bytes are unconstrained: 2^8 x 4 x 2^16 combinations,
 * including the 246 type codes the general format cannot carry and the byte
 * pairs no conforming sender emits. CBMC covers them symbolically.
 *
 * This is a unit test whose inputs are arbitrary, not a function contract:
 * nothing here says what the decoder should do, only that it does what the
 * equations say. The equations were checked first -- see lab/docs/method.md.
 */
#include "core/taiseia/general.h"

#include "reference.h"

/* Left undefined on purpose: CBMC models an undefined function as returning an
 * unconstrained value of its return type. */
uint8_t nondet_u8(void);

int main(void)
{
    uint8_t  dtype   = nondet_u8();
    uint8_t  reading = nondet_u8();
    uint8_t  b[TS_GENERAL_DATA_BYTES];
    int32_t  got     = 0;
    int32_t  want    = 0;
    result_t rc;
    int      accepted;

    b[0] = nondet_u8();
    b[1] = nondet_u8();

    /* The only assumption: a reading the caller could name. Everything a caller
     * cannot get wrong stays unconstrained, including the type code. */
    __CPROVER_assume(reading <= (uint8_t)TS_READ_LENIENT);

    rc       = ts_general_decode(dtype, (ts_reading_t)reading, b,
                                 sizeof b, &got);
    accepted = ref_decode(dtype, (int)reading, b, &want);

    /*
     * The implementation distinguishes an unsupported type from a refused byte
     * pair; the reference says only "accepted" or not. Both answer the question
     * that matters: did a value come out?
     */
    __CPROVER_assert((rc == RES_OK) == (accepted == 1),
                     "decode accepts exactly what the equations accept");

    if (rc == RES_OK)
        __CPROVER_assert(got == want,
                         "decode returns the value the equations give");

    return 0;
}
