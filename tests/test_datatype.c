/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "core/taiseia/datatype.h"

static void known_codes_resolve(void)
{
    const ts_datatype_t *t;

    t = ts_datatype(0x21);
    CHECK(t != NULL);
    if (t) {
        CHECK_STREQ(t->name, "UINT16");
        CHECK_EQ(t->value_len, 2);
        CHECK_EQ(t->support_len, 4);
        CHECK_EQ(t->support, TS_SUPPORT_RANGE);
        CHECK_EQ(t->is_signed, 0);
    }

    t = ts_datatype(0x31);
    CHECK(t != NULL);
    if (t) {
        CHECK_STREQ(t->name, "INT16");
        CHECK_EQ(t->is_signed, 1);
    }

    CHECK(ts_datatype(0x02) == NULL);
    CHECK(ts_datatype(0xAA) == NULL);
    CHECK_STREQ(ts_datatype_name(0xAA), "unknown");
}

/*
 * An enumeration and a bit type both carry a mask, and they do not mean the
 * same thing. Keeping them distinguishable is the whole reason the type code
 * is published alongside the capability.
 */
static void enumerations_and_bit_types_are_both_masks_but_distinct(void)
{
    const ts_datatype_t *e = ts_datatype(0x01);
    const ts_datatype_t *b = ts_datatype(0x10);

    CHECK(e != NULL);
    CHECK(b != NULL);
    if (e && b) {
        CHECK_EQ(e->support, TS_SUPPORT_BITS);
        CHECK_EQ(b->support, TS_SUPPORT_BITS);
        CHECK(e->code != b->code);
        CHECK_STREQ(e->name, "ENUM16");
        CHECK_STREQ(b->name, "ENUM16_Bit");
    }
}

static void composite_types_are_a_timestamp_plus_a_scalar(void)
{
    const ts_datatype_t *t = ts_datatype(0x72);   /* stamp + UINT32 */

    CHECK(t != NULL);
    if (t) {
        CHECK_EQ(t->value_len, 6 + 4);
        CHECK_EQ(t->support_len, 6 + 8);
        CHECK_EQ(t->support, TS_SUPPORT_COMPOSITE);
    }
}

/* The published table gives INT64 a four-byte value; see the note in
 * datatype.c. We implement eight, consistently with UINT64. */
static void int64_is_eight_bytes_wide(void)
{
    const ts_datatype_t *s = ts_datatype(0x33);
    const ts_datatype_t *u = ts_datatype(0x23);

    CHECK(s != NULL);
    CHECK(u != NULL);
    if (s && u) {
        CHECK_EQ(s->value_len, 8);
        CHECK_EQ(s->value_len, u->value_len);
        CHECK_EQ(s->support_len, u->support_len);
    }
}

static void the_table_is_internally_consistent(void)
{
    size_t i, j;

    CHECK(ts_datatype_count() > 0);
    CHECK(ts_datatype_at(ts_datatype_count()) == NULL);

    for (i = 0; i < ts_datatype_count(); i++) {
        const ts_datatype_t *a = ts_datatype_at(i);

        CHECK(a != NULL);
        if (!a)
            continue;

        /* Every entry is reachable by its own code. */
        CHECK(ts_datatype(a->code) == a);

        /* No duplicate codes. */
        for (j = i + 1; j < ts_datatype_count(); j++) {
            const ts_datatype_t *b = ts_datatype_at(j);

            CHECK(b != NULL);
            if (b)
                CHECK(a->code != b->code);
        }
    }
}

static void integers_are_big_endian(void)
{
    const uint8_t two[]  = { 0x12, 0x34 };
    const uint8_t four[] = { 0x00, 0x00, 0x01, 0x00 };
    int64_t       v = 0;

    CHECK_EQ(ts_decode_int(two, 2, false, &v), RES_OK);
    CHECK_EQ(v, 0x1234);

    CHECK_EQ(ts_decode_int(four, 4, false, &v), RES_OK);
    CHECK_EQ(v, 256);
}

/*
 * Negative values are two's complement on the wire. Reading them as unsigned
 * is silent and produces a plausible number, which is what makes it dangerous.
 */
static void signed_values_are_sign_extended(void)
{
    const uint8_t one[]   = { 0xFF };
    const uint8_t two[]   = { 0xFF, 0xFD };
    const uint8_t four[]  = { 0xFF, 0xFF, 0xFF, 0xFF };
    const uint8_t eight[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE };
    int64_t       v = 0;

    CHECK_EQ(ts_decode_int(one, 1, true, &v), RES_OK);
    CHECK_EQ(v, -1);
    CHECK_EQ(ts_decode_int(one, 1, false, &v), RES_OK);
    CHECK_EQ(v, 255);

    CHECK_EQ(ts_decode_int(two, 2, true, &v), RES_OK);
    CHECK_EQ(v, -3);
    CHECK_EQ(ts_decode_int(two, 2, false, &v), RES_OK);
    CHECK_EQ(v, 65533);

    CHECK_EQ(ts_decode_int(four, 4, true, &v), RES_OK);
    CHECK_EQ(v, -1);

    CHECK_EQ(ts_decode_int(eight, 8, true, &v), RES_OK);
    CHECK_EQ(v, -2);

    /* A positive value must not be corrupted by the sign path. */
    {
        const uint8_t pos[] = { 0x7F, 0xFF };

        CHECK_EQ(ts_decode_int(pos, 2, true, &v), RES_OK);
        CHECK_EQ(v, 32767);
    }
}

static void decode_rejects_widths_it_cannot_represent(void)
{
    const uint8_t buf[8] = { 0 };
    int64_t       v = 0;

    CHECK_EQ(ts_decode_int(buf, 0, false, &v), RES_EINVAL);
    CHECK_EQ(ts_decode_int(buf, 3, false, &v), RES_EINVAL);
    CHECK_EQ(ts_decode_int(buf, 5, false, &v), RES_EINVAL);
    CHECK_EQ(ts_decode_int(buf, 9, false, &v), RES_EINVAL);
    CHECK_EQ(ts_decode_int(NULL, 2, false, &v), RES_EINVAL);
    CHECK_EQ(ts_decode_int(buf, 2, false, NULL), RES_EINVAL);
}

void test_datatype(void)
{
    known_codes_resolve();
    enumerations_and_bit_types_are_both_masks_but_distinct();
    composite_types_are_a_timestamp_plus_a_scalar();
    int64_is_eight_bytes_wide();
    the_table_is_internally_consistent();
    integers_are_big_endian();
    signed_values_are_sign_extended();
    decode_rejects_widths_it_cannot_represent();
}
