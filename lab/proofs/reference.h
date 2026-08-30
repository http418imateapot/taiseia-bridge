/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Equations (2) to (4) transcribed into C, for the proofs to compare against.
 *
 * This is a THIRD transcription of the same prose. lab/oracle/layouts.py and
 * lab/oracle/readings.py are the first, lab/oracle/smt.py is the second, and
 * this is the one a model checker can put next to the implementation. It is
 * written to mirror the Python line for line rather than to mirror
 * src/core/taiseia/general.c -- a reference that was derived from the code it
 * judges would agree with the code for the wrong reason.
 *
 * WHAT IT SHARES WITH THE IMPLEMENTATION, DELIBERATELY. Both ask
 * `ts_datatype()` for a type's width and signedness. The type table is a
 * separate premise, audited in lab/docs/oracle-audit.md (B1, B2) and swept by
 * lab/oracle/sensitivity.py; keeping a second copy of it here would be the
 * drift this directory exists to avoid. So the proofs establish that the codec
 * agrees with the equations GIVEN the table, and say nothing about the table.
 *
 * Returns are 1 for accepted and 0 for refused, rather than `result_t`, so that
 * the reference does not inherit the implementation's vocabulary.
 */
#ifndef TSB_LAB_PROOFS_REFERENCE_H
#define TSB_LAB_PROOFS_REFERENCE_H

#include <stdint.h>

/* Layouts, matching lab/oracle/layouts.py. */
#define REF_LAYOUT_A 0  /* sign-extend: the field is one 16-bit two's compl. */
#define REF_LAYOUT_B 1  /* zero-fill:   value in the low byte, high byte 00  */

/* Readings, matching lab/oracle/readings.py. */
#define REF_READ_WIDE    0
#define REF_READ_LOW     1
#define REF_READ_STRICT  2
#define REF_READ_LENIENT 3

/** 1 when the general format can carry this type, with its width and sign. */
int ref_carryable(uint8_t dtype, unsigned *width, int *is_signed);

/** V_t, the domain of a type of this width and signedness. */
void ref_domain(unsigned width, int is_signed, int32_t *lo, int32_t *hi);

/** Equation (2). 1 and fills `out` when `v` is in V_t, else 0. */
int ref_encode(uint8_t dtype, int layout, int32_t v, uint8_t *out);

/** Equations (3) and (4). 1 and fills `out` when accepted, else 0. */
int ref_decode(uint8_t dtype, int reading, const uint8_t *in, int32_t *out);

#endif /* TSB_LAB_PROOFS_REFERENCE_H */
