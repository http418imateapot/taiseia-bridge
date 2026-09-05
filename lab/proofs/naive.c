/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE OBVIOUS VERSION, KEPT SO THE PROOF CAN BE SHOWN CATCHING IT.
 *
 * This is not the implementation and nothing links it into the program. It is
 * how sign extension gets written when nobody is thinking about C99 6.3.1.3,
 * and it is here as a demonstration: `make demo` expects CBMC to REJECT it.
 *
 * The problem is that converting to a signed type a value the type cannot
 * represent is implementation-defined -- and the values that cannot be
 * represented are exactly the ones these two functions exist to convert. On gcc
 * and on MSVC the result is the two's complement answer, which is what was
 * wanted, so:
 *
 *   - the exhaustive unit test passes;
 *   - both CI compilers agree;
 *   - UBSan says nothing, because this is implementation-defined, not undefined;
 *
 * and the code is still not right, because none of that is a property of the
 * source. This is the gap between "the tests pass" and "the code is correct",
 * and it is the one thing in this directory that only a model checker closes.
 */
#include "naive.h"

int32_t naive_sigma8(uint8_t lo)
{
    return (int8_t)lo;   /* implementation-defined for lo > 127 */
}

int32_t naive_sigma16(uint16_t field)
{
    return (int16_t)field;   /* implementation-defined for field > 32767 */
}

/* The same two conversions written so that C99 defines them. This is what
 * src/core/taiseia/general.c does, and what the demo compares against. */
int32_t defined_sigma8(uint8_t lo)
{
    if (lo >= 128u)
        return (int32_t)lo - 256;
    return (int32_t)lo;
}

int32_t defined_sigma16(uint16_t field)
{
    if (field >= 32768u)
        return (int32_t)field - 65536;
    return (int32_t)field;
}
