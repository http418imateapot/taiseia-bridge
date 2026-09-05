/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The general format's two-byte data field, typed.
 *
 * A general-format service answers with two bytes and nothing that says what
 * they are. Given the type from somewhere else -- the operator's own map, see
 * docs/adr/0014 -- this reads a value out of them, or writes one in.
 *
 * TWO RULES THAT DISAGREE.
 *
 * The standard says negatives are two's complement, and it says a one-byte
 * value goes in the low byte with the high byte zero. For a one-byte negative
 * value those give different bytes: -3 is FF FD by the first and 00 FD by the
 * second. A sender picks one and the wire does not record which.
 *
 * So the choice is a parameter here rather than a constant. `ts_layout_t` is
 * what a sender does; `ts_reading_t` is what a receiver does. All four readings
 * are present because the matrix that justifies the default is only checkable
 * if the alternatives exist -- see lab/docs/oracle-audit.md, entry A2.
 *
 * Of the eight one-byte combinations, two misread: sign-extended bytes read
 * strictly, and zero-filled bytes read whole. No layout is safe against both,
 * so a sender cannot close this alone. TS_READ_LENIENT is the only reading that
 * accepts a byte pair if and only if some conforming sender emits it, and is
 * the reason it is the recommended default for a receiver.
 *
 * Nothing in this program calls this yet. It is pure, it allocates nothing, and
 * wiring it to the published payload is ADR 0014's business, not this file's.
 */
#ifndef TSB_CORE_TAISEIA_GENERAL_H
#define TSB_CORE_TAISEIA_GENERAL_H

#include "core/util/result.h"

#include <stddef.h>
#include <stdint.h>

/* The data field of a general-format packet. Always exactly this wide. */
#define TS_GENERAL_DATA_BYTES 2u

/** What a sender does with a one-byte value in a two-byte field. */
typedef enum {
    TS_LAYOUT_SIGN_EXTEND = 0,  /* the field is one 16-bit two's complement */
    TS_LAYOUT_ZERO_FILL         /* the value is the low byte, high byte 00  */
} ts_layout_t;

/**
 * What a receiver does with the bytes above the value.
 *
 * For a two-byte type there are none, and all four coincide with "read the
 * field". The distinction only bites at one byte wide.
 */
typedef enum {
    TS_READ_WIDE = 0,  /* read the whole field, then range-check it        */
    TS_READ_LOW,       /* read the low byte, ignore whatever is above it   */
    TS_READ_STRICT,    /* read the low byte, the high byte must be 00      */
    TS_READ_LENIENT    /* as strict, but FF is allowed when the value is
                          negative -- accepts both layouts and nothing else */
} ts_reading_t;

/**
 * The domain of a type: the least and greatest value it can hold.
 *
 * RES_EUNSUPPORTED when the type is not one the general format can carry --
 * anything wider than two bytes, and the variable-length string. Refused
 * rather than truncated: a truncated value looks plausible and is wrong.
 */
result_t ts_general_domain(uint8_t dtype, int32_t *lo, int32_t *hi);

/**
 * Write `value` into `out` as `layout` would.
 *
 * RES_EINVAL if `value` is outside the type's domain, which is not something a
 * conforming sender emits, or if the buffer is too small.
 */
result_t ts_general_encode(uint8_t dtype, ts_layout_t layout, int32_t value,
                           uint8_t *out, size_t cap);

/**
 * Read a value out of `in` as `reading` would.
 *
 * RES_EFORMAT when the reading refuses the byte pair -- the bytes above the
 * value are ones no sender using that rule would have written, or the value
 * they carry is outside the type's domain. A refusal is the point: it is the
 * only signal available that the sender and the receiver disagree about the
 * layout, and it is better than a plausible wrong number.
 */
result_t ts_general_decode(uint8_t dtype, ts_reading_t reading,
                           const uint8_t *in, size_t len, int32_t *out);

#endif /* TSB_CORE_TAISEIA_GENERAL_H */
