/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The general-format codec, walked exhaustively.
 *
 * These counts are not free-standing expectations: they must equal the ones in
 * lab/results/matrix.json, which were produced twice over -- once by
 * enumeration in Python and once by z3 -- before this file was written. If this
 * test and that file disagree, one of the three transcriptions is wrong and the
 * question of which is not answered by making the test pass.
 *
 * What this cannot establish is that the source is right rather than that this
 * compiler agrees: see lab/proofs.
 */
#include "tests.h"

#include "core/taiseia/datatype.h"
#include "core/taiseia/general.h"

#include <stdint.h>

#define INT8_CODE   0x30u
#define INT16_CODE  0x31u
#define UINT8_CODE  0x20u
#define UINT16_CODE 0x21u
#define INT32_CODE  0x32u   /* four bytes -- the format cannot carry it */
#define STRING_CODE 0x50u   /* variable length -- likewise             */

/* ------------------------------------------------------------- the type set */

/*
 * Ten types fit the two-byte field: eight of width two, and UINT8 and INT8. The
 * lab derives this same set by reading datatype.c; this asserts it from the
 * other side, so the two cannot drift apart unnoticed.
 */
static void ten_types_fit_the_two_byte_field(void)
{
    size_t i;
    int    carryable = 0;
    int    width_two = 0;
    int    width_one = 0;

    for (i = 0; i < ts_datatype_count(); i++) {
        const ts_datatype_t *t = ts_datatype_at(i);
        int32_t              lo = 0;
        int32_t              hi = 0;

        if (ts_general_domain(t->code, &lo, &hi) != RES_OK)
            continue;

        carryable++;
        if (t->value_len == 2u)
            width_two++;
        else
            width_one++;
    }

    CHECK_EQ(carryable, 10);
    CHECK_EQ(width_two, 8);
    CHECK_EQ(width_one, 2);
}

static void a_type_too_wide_to_carry_is_refused(void)
{
    int32_t lo = 0;
    int32_t hi = 0;
    uint8_t b[TS_GENERAL_DATA_BYTES] = { 0x00, 0x00 };
    int32_t v = 0;

    CHECK_EQ(ts_general_domain(INT32_CODE, &lo, &hi), RES_EUNSUPPORTED);
    CHECK_EQ(ts_general_domain(STRING_CODE, &lo, &hi), RES_EUNSUPPORTED);
    CHECK_EQ(ts_general_domain(0xFFu, &lo, &hi), RES_EUNSUPPORTED);

    /* Refused, not truncated: four bytes squeezed into two is a plausible
     * wrong number, which is the outcome this project prefers to avoid. */
    CHECK_EQ(ts_general_encode(INT32_CODE, TS_LAYOUT_SIGN_EXTEND, 1,
                               b, sizeof b), RES_EUNSUPPORTED);
    CHECK_EQ(ts_general_decode(INT32_CODE, TS_READ_LENIENT, b, sizeof b, &v),
             RES_EUNSUPPORTED);
}

static void the_domains_are_the_ones_the_widths_imply(void)
{
    int32_t lo = 0;
    int32_t hi = 0;

    CHECK_EQ(ts_general_domain(INT8_CODE, &lo, &hi), RES_OK);
    CHECK_EQ(lo, -128);
    CHECK_EQ(hi, 127);

    CHECK_EQ(ts_general_domain(UINT8_CODE, &lo, &hi), RES_OK);
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 255);

    CHECK_EQ(ts_general_domain(INT16_CODE, &lo, &hi), RES_OK);
    CHECK_EQ(lo, -32768);
    CHECK_EQ(hi, 32767);

    CHECK_EQ(ts_general_domain(UINT16_CODE, &lo, &hi), RES_OK);
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 65535);
}

/* --------------------------------------------------- the worked example, -3 */

/*
 * The two layouts and the four readings, on the value the whole question is
 * usually stated with. Everything below is the general case; this is the one a
 * reader can check by hand.
 */
static void minus_three_is_ff_fd_or_00_fd_and_read_four_ways(void)
{
    uint8_t a[TS_GENERAL_DATA_BYTES] = { 0, 0 };
    uint8_t b[TS_GENERAL_DATA_BYTES] = { 0, 0 };
    const uint8_t junk[TS_GENERAL_DATA_BYTES] = { 0x12, 0xFD };
    int32_t v = 0;

    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_SIGN_EXTEND, -3,
                               a, sizeof a), RES_OK);
    CHECK_EQ(a[0], 0xFF);
    CHECK_EQ(a[1], 0xFD);

    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_ZERO_FILL, -3,
                               b, sizeof b), RES_OK);
    CHECK_EQ(b[0], 0x00);
    CHECK_EQ(b[1], 0xFD);

    /* wide: takes the sign-extended pair, refuses the zero-filled one because
     * 253 is past the top of INT8. */
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_WIDE, a, sizeof a, &v),
             RES_OK);
    CHECK_EQ(v, -3);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_WIDE, b, sizeof b, &v),
             RES_EFORMAT);

    /* strict: the mirror image. */
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_STRICT, b, sizeof b, &v),
             RES_OK);
    CHECK_EQ(v, -3);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_STRICT, a, sizeof a, &v),
             RES_EFORMAT);

    /* lenient: both, and not the third. */
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, a, sizeof a, &v),
             RES_OK);
    CHECK_EQ(v, -3);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, b, sizeof b, &v),
             RES_OK);
    CHECK_EQ(v, -3);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, junk, sizeof junk,
                               &v), RES_EFORMAT);

    /* low: all three, which is the problem with it. 12 FD is a byte pair no
     * conforming sender emits, and low reports -3 for it without complaint. */
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LOW, junk, sizeof junk, &v),
             RES_OK);
    CHECK_EQ(v, -3);
}

/* ----------------------------------------------------------- the 80 cells */

/*
 * Every carryable type, both layouts, all four readings, every value of every
 * domain: 80 cells, and the counts must equal lab/results/matrix.json.
 *
 * The per-value comparison is deliberately not a CHECK. Four million assertions
 * would drown the log and say nothing the cell counts do not; the cell is the
 * unit of the claim.
 */
static void only_int8_misreads_and_only_in_two_cells(void)
{
    size_t i;
    int    cells = 0;
    int    failing_cells = 0;

    for (i = 0; i < ts_datatype_count(); i++) {
        const ts_datatype_t *t = ts_datatype_at(i);
        int32_t              lo = 0;
        int32_t              hi = 0;
        int                  layout;

        if (ts_general_domain(t->code, &lo, &hi) != RES_OK)
            continue;

        for (layout = 0; layout <= (int)TS_LAYOUT_ZERO_FILL; layout++) {
            int reading;

            for (reading = 0; reading <= (int)TS_READ_LENIENT; reading++) {
                int32_t v;
                int     misread = 0;

                cells++;

                for (v = lo; v <= hi; v++) {
                    uint8_t b[TS_GENERAL_DATA_BYTES];
                    int32_t back = 0;

                    if (ts_general_encode(t->code, (ts_layout_t)layout, v,
                                          b, sizeof b) != RES_OK) {
                        misread++;
                        continue;
                    }
                    if (ts_general_decode(t->code, (ts_reading_t)reading,
                                          b, sizeof b, &back) != RES_OK ||
                        back != v)
                        misread++;
                }

                if (misread > 0) {
                    failing_cells++;
                    /* Both failing cells are INT8, and both lose exactly the
                     * 128 negative values. */
                    CHECK_EQ(t->code, INT8_CODE);
                    CHECK_EQ(misread, 128);
                    CHECK(((layout == (int)TS_LAYOUT_SIGN_EXTEND &&
                            reading == (int)TS_READ_STRICT) ||
                           (layout == (int)TS_LAYOUT_ZERO_FILL &&
                            reading == (int)TS_READ_WIDE)));
                }
            }
        }
    }

    CHECK_EQ(cells, 80);
    CHECK_EQ(failing_cells, 2);
}

/*
 * Proposition 2: a lenient receiver recovers every value under either layout,
 * for every type. This is the whole argument for the recommended default.
 */
static void a_lenient_receiver_recovers_every_value(void)
{
    size_t i;
    int    checked_types = 0;

    for (i = 0; i < ts_datatype_count(); i++) {
        const ts_datatype_t *t = ts_datatype_at(i);
        int32_t              lo = 0;
        int32_t              hi = 0;
        int                  layout;
        int                  lost = 0;

        if (ts_general_domain(t->code, &lo, &hi) != RES_OK)
            continue;

        checked_types++;

        for (layout = 0; layout <= (int)TS_LAYOUT_ZERO_FILL; layout++) {
            int32_t v;

            for (v = lo; v <= hi; v++) {
                uint8_t b[TS_GENERAL_DATA_BYTES];
                int32_t back = 0;

                if (ts_general_encode(t->code, (ts_layout_t)layout, v,
                                      b, sizeof b) != RES_OK ||
                    ts_general_decode(t->code, TS_READ_LENIENT, b, sizeof b,
                                      &back) != RES_OK ||
                    back != v)
                    lost++;
            }
        }

        CHECK_EQ(lost, 0);
    }

    CHECK_EQ(checked_types, 10);
}

/*
 * Proposition 3, both directions: lenient accepts a byte pair if and only if
 * some conforming sender emits it.
 *
 * The second direction is the one that is easy to leave out, and leaving it out
 * is how `low` -- which never misreads anything -- passes for a safe reading
 * while reading 12 FD as -3.
 */
static void a_lenient_receiver_accepts_exactly_what_a_sender_sends(void)
{
    /* One bit per byte pair. Building the image once and then testing each pair
     * against it keeps this linear; searching the domain per pair would be
     * 2^32 encodes for the two-byte types. */
    static unsigned char image[65536u / 8u];

    const uint8_t codes[] = { INT8_CODE, UINT8_CODE, INT16_CODE, UINT16_CODE };
    size_t        c;

    for (c = 0; c < sizeof codes / sizeof codes[0]; c++) {
        int32_t  lo = 0;
        int32_t  hi = 0;
        int      over = 0;
        int      under = 0;
        int      layout;
        unsigned pair;
        size_t   k;

        CHECK_EQ(ts_general_domain(codes[c], &lo, &hi), RES_OK);

        for (k = 0; k < sizeof image; k++)
            image[k] = 0;

        for (layout = 0; layout <= (int)TS_LAYOUT_ZERO_FILL; layout++) {
            int32_t v;

            for (v = lo; v <= hi; v++) {
                uint8_t  e[TS_GENERAL_DATA_BYTES];
                unsigned idx;

                if (ts_general_encode(codes[c], (ts_layout_t)layout, v,
                                      e, sizeof e) != RES_OK)
                    continue;

                idx = ((unsigned)e[0] << 8) | (unsigned)e[1];
                image[idx >> 3] |= (unsigned char)(1u << (idx & 7u));
            }
        }

        for (pair = 0; pair < 65536u; pair++) {
            uint8_t b[TS_GENERAL_DATA_BYTES];
            int32_t back = 0;
            int     accepted;
            int     emitted;

            b[0] = (uint8_t)(pair >> 8);
            b[1] = (uint8_t)(pair & 0xFFu);

            accepted = ts_general_decode(codes[c], TS_READ_LENIENT,
                                         b, sizeof b, &back) == RES_OK;
            emitted  = (image[pair >> 3] >> (pair & 7u)) & 1u;

            if (accepted && !emitted)
                over++;
            if (!accepted && emitted)
                under++;
        }

        CHECK_EQ(over, 0);
        CHECK_EQ(under, 0);
    }
}

/* ------------------------------------------------------------------ refusals */

static void bad_arguments_are_refused_not_guessed(void)
{
    uint8_t b[TS_GENERAL_DATA_BYTES] = { 0x00, 0x01 };
    int32_t v = 0;
    int32_t lo = 0;
    int32_t hi = 0;

    CHECK_EQ(ts_general_domain(INT8_CODE, NULL, &hi), RES_EINVAL);
    CHECK_EQ(ts_general_domain(INT8_CODE, &lo, NULL), RES_EINVAL);

    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_ZERO_FILL, 0, NULL, 2),
             RES_EINVAL);
    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_ZERO_FILL, 0, b, 1),
             RES_EINVAL);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, NULL, 2, &v),
             RES_EINVAL);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, b, 1, &v),
             RES_EINVAL);
    CHECK_EQ(ts_general_decode(INT8_CODE, TS_READ_LENIENT, b, 2, NULL),
             RES_EINVAL);

    /* A value the type cannot hold is a caller error, not a wire error. */
    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_SIGN_EXTEND, 128,
                               b, sizeof b), RES_EINVAL);
    CHECK_EQ(ts_general_encode(INT8_CODE, TS_LAYOUT_SIGN_EXTEND, -129,
                               b, sizeof b), RES_EINVAL);
    CHECK_EQ(ts_general_encode(UINT8_CODE, TS_LAYOUT_ZERO_FILL, -1,
                               b, sizeof b), RES_EINVAL);
}

void test_general(void)
{
    ten_types_fit_the_two_byte_field();
    a_type_too_wide_to_carry_is_refused();
    the_domains_are_the_ones_the_widths_imply();
    minus_three_is_ff_fd_or_00_fd_and_read_four_ways();
    only_int8_misreads_and_only_in_two_cells();
    a_lenient_receiver_recovers_every_value();
    a_lenient_receiver_accepts_exactly_what_a_sender_sends();
    bad_arguments_are_refused_not_guessed();
}
