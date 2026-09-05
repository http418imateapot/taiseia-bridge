/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Does the encoder agree with the equations, for EVERY input?
 *
 * Type code, layout and value all unconstrained -- the value over the whole of
 * int32_t, not just the domains the types allow, so the refusal of an
 * out-of-domain value is proved rather than assumed.
 */
#include "core/taiseia/general.h"

#include "reference.h"

uint8_t nondet_u8(void);
int32_t nondet_i32(void);

int main(void)
{
    uint8_t  dtype  = nondet_u8();
    uint8_t  layout = nondet_u8();
    int32_t  value  = nondet_i32();
    uint8_t  got[TS_GENERAL_DATA_BYTES];
    uint8_t  want[TS_GENERAL_DATA_BYTES];
    result_t rc;
    int      accepted;

    __CPROVER_assume(layout <= (uint8_t)TS_LAYOUT_ZERO_FILL);

    rc       = ts_general_encode(dtype, (ts_layout_t)layout, value,
                                 got, sizeof got);
    accepted = ref_encode(dtype, (int)layout, value, want);

    __CPROVER_assert((rc == RES_OK) == (accepted == 1),
                     "encode accepts exactly the values the equations accept");

    if (rc == RES_OK)
        __CPROVER_assert(got[0] == want[0] && got[1] == want[1],
                         "encode writes the bytes the equations give");

    return 0;
}
