/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The demonstration harness. This one is EXPECTED TO FAIL.
 *
 * `make demo` runs it and treats a clean verification as the error, because the
 * point being made is that CBMC rejects the obvious way of writing sign
 * extension while every test in this repository accepts it.
 *
 * Two conversions, two findings -- one per function.
 */
#include "naive.h"

uint8_t  nondet_u8(void);
uint16_t nondet_u16(void);

int main(void)
{
    uint8_t  lo    = nondet_u8();
    uint16_t field = nondet_u16();

    /* --conversion-check fires inside naive_sigma8 and naive_sigma16, before
     * these assertions are even reached. The assertions are here so that the
     * harness states what the functions were supposed to compute. */
    __CPROVER_assert(naive_sigma8(lo) == defined_sigma8(lo),
                     "the obvious 8-bit sign extension is defined");
    __CPROVER_assert(naive_sigma16(field) == defined_sigma16(field),
                     "the obvious 16-bit sign extension is defined");

    return 0;
}
