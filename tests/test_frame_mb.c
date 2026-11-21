/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multi-byte framing, byte for byte against CNS 16014:2025 tables 14 and 15.
 *
 * Every field in this header is a place to be one byte out, and every such
 * mistake decodes into something that still looks like data. An earlier
 * version of the layout put the service id in byte 2 and was three bytes
 * short from the reserved field on; the simulator agreed, because it builds
 * its replies with the functions under test. So the packets here are written
 * out by hand from the tables, and the builders and the parser are judged
 * against them -- never against each other alone.
 */
#include "tests.h"

#include "core/taiseia/frame.h"

static void a_multibyte_read_request_is_exact(void)
{
    uint8_t out[32];
    size_t  len = 0;
    static const uint8_t want[] = {
        0x11,                    /* length: 13 overhead + 4 data          */
        0xE0,                    /* SA id                                  */
        0x00,                    /* service type: read, bits 6..0 zero     */
        0x00, 0x20,              /* service id 0x0020, two bytes           */
        0x00, 0x00,              /* reserved, 0x0000                       */
        0x00, 0x00,              /* fragment: we never send one            */
        0x22,                    /* data type UINT32                       */
        0xFF, 0xFF,              /* item count: asking, not telling        */
        0xFF, 0xFF, 0xFF, 0xFF,  /* one item wide, every byte 0xFF         */
        0x00                     /* checksum                               */
    };
    uint8_t expect[sizeof(want)];

    memcpy(expect, want, sizeof(want));
    expect[sizeof(want) - 1u] = ts_checksum(expect, sizeof(want) - 1u);

    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x22, 4),
             RES_OK);
    CHECK_EQ(len, sizeof(want));
    CHECK(memcmp(out, expect, sizeof(want)) == 0);
}

static void a_multibyte_write_request_is_exact(void)
{
    uint8_t out[32];
    size_t  len = 0;
    static const uint8_t data[] = { 0x00, 0x10, 0x00, 0x20 };
    static const uint8_t want[] = {
        0x11,                    /* length: 13 overhead + 4 data          */
        0xE0,                    /* SA id                                  */
        0x80,                    /* service type: write                    */
        0x00, 0x21,              /* service id 0x0021                      */
        0x00, 0x00,              /* reserved                               */
        0x00, 0x00,              /* fragment                               */
        0x21,                    /* data type UINT16                       */
        0x00, 0x02,              /* two items                              */
        0x00, 0x10, 0x00, 0x20,  /* the items                              */
        0x00                     /* checksum                               */
    };
    uint8_t expect[sizeof(want)];

    memcpy(expect, want, sizeof(want));
    expect[sizeof(want) - 1u] = ts_checksum(expect, sizeof(want) - 1u);

    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x21, 0x21, 2,
                               data, sizeof(data)), RES_OK);
    CHECK_EQ(len, sizeof(want));
    CHECK(memcmp(out, expect, sizeof(want)) == 0);
}

static void a_multibyte_reply_is_read_at_the_tables_offsets(void)
{
    /* Written out by hand. Each field holds a value no other field holds, so
     * reading any of them from a neighbour's offset fails a check below. */
    uint8_t pkt[] = {
        0x0F,                    /* length: 13 overhead + 2 data           */
        0xE0,                    /* SA id                                  */
        0x80,                    /* service type: write                    */
        0x01, 0x23,              /* service id 0x0123                      */
        0x45, 0x67,              /* reserved, deliberately not zero        */
        0x00, 0x03,              /* fragment 3                             */
        0x21,                    /* data type UINT16                       */
        0x00, 0x01,              /* one item                               */
        0x12, 0x34,              /* the item                               */
        0x00                     /* checksum                               */
    };
    ts_mb_frame_t f;
    uint16_t      frag = 0;

    pkt[sizeof(pkt) - 1u] = ts_checksum(pkt, sizeof(pkt) - 1u);

    CHECK_EQ(ts_parse_mb_response(pkt, sizeof(pkt), &f), RES_OK);
    CHECK_EQ(f.sa_id, 0xE0);
    CHECK_EQ(f.write, 1);
    CHECK_EQ(f.service_type, 0x80);
    CHECK_EQ(f.service_id, 0x0123);
    CHECK_EQ(f.reserved, 0x4567);
    CHECK_EQ(f.fragment, 3);
    CHECK_EQ(f.dtype, 0x21);
    CHECK_EQ(f.count, 1);
    CHECK_EQ(f.data_len, 2);
    CHECK_EQ((f.data[0] << 8) | f.data[1], 0x1234);

    /* And the fragment can be had without decoding the rest, which is what
     * the transport needs while it is still gathering pieces. */
    CHECK_EQ(ts_reply_fragment(pkt, sizeof(pkt), TS_REPLY_MULTIBYTE, &frag),
             RES_OK);
    CHECK_EQ(frag, 3);
}

/*
 * The service id has two bytes of its own, and byte 2 is not part of it. A
 * builder that still packed the id into byte 2 would set bits there that the
 * standard reserves, and would lose the id's high byte.
 */
static void the_service_id_is_two_bytes_and_not_in_byte_2(void)
{
    uint8_t       out[32];
    size_t        len = 0;
    ts_mb_frame_t f;
    static const uint8_t data[] = { 0x7F };

    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x0181, 0x20, 1,
                               data, sizeof(data)), RES_OK);
    CHECK_EQ(out[2], 0x80);          /* the write bit, and nothing else */
    CHECK_EQ(out[3], 0x01);
    CHECK_EQ(out[4], 0x81);

    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x0181, 0x20, 1),
             RES_OK);
    CHECK_EQ(out[2], 0x00);
    CHECK_EQ(out[3], 0x01);
    CHECK_EQ(out[4], 0x81);

    CHECK_EQ(ts_parse_mb_response(out, len, &f), RES_OK);
    CHECK_EQ(f.service_id, 0x0181);
    CHECK_EQ(f.write, 0);
}

/*
 * The reserved bits and bytes are written as zero and reported as received.
 * Refusing a non-zero value would make every reply from an appliance built to
 * a later revision unreadable, for a field the standard keeps for exactly that.
 */
static void reserved_fields_are_sent_as_zero_and_not_refused(void)
{
    uint8_t       pkt[32];
    size_t        len = 0;
    ts_mb_frame_t f;
    static const uint8_t data[] = { 0x01, 0x02 };

    CHECK_EQ(ts_build_mb_write(pkt, sizeof(pkt), &len, 0xE0, 0x21, 0x21, 1,
                               data, sizeof(data)), RES_OK);
    CHECK_EQ(pkt[2] & 0x7F, 0x00);
    CHECK_EQ(pkt[5], 0x00);
    CHECK_EQ(pkt[6], 0x00);

    pkt[2] = (uint8_t)(pkt[2] | 0x05u);
    pkt[5] = 0xAB;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);

    CHECK_EQ(ts_parse_mb_response(pkt, len, &f), RES_OK);
    CHECK_EQ(f.write, 1);
    CHECK_EQ(f.service_type, 0x85);
    CHECK_EQ(f.service_id, 0x0021);
    CHECK_EQ(f.reserved, 0xAB00);
}

static void the_fragment_number_is_found_wherever_it_lives(void)
{
    uint16_t frag = 99;
    /* A registration payload keeps it in the high nibble of its fourth byte,
     * sharing with the link speed -- which is packet byte 5. */
    static const uint8_t reg[] = { 0x0A, 0x00, 0x00, 0x01, 0x00, 0x23,
                                   0x00, 0x04, 0x00, 0x00 };
    static const uint8_t general[] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };

    CHECK_EQ(ts_reply_fragment(reg, sizeof(reg), TS_REPLY_REGISTRATION,
                               &frag), RES_OK);
    CHECK_EQ(frag, 2);

    /* A general reply is six bytes and has nowhere to put one. */
    CHECK_EQ(ts_reply_fragment(general, sizeof(general), TS_REPLY_GENERAL,
                               &frag), RES_OK);
    CHECK_EQ(frag, 0);

    CHECK_EQ(ts_reply_fragment(reg, 4, TS_REPLY_REGISTRATION, &frag),
             RES_EFORMAT);
    /* Ten bytes held the old, short header; it does not hold this one. */
    CHECK_EQ(ts_reply_fragment(reg, sizeof(reg), TS_REPLY_MULTIBYTE, &frag),
             RES_EFORMAT);
    CHECK_EQ(ts_reply_fragment(NULL, 10, TS_REPLY_MULTIBYTE, &frag),
             RES_EINVAL);
    CHECK_EQ(ts_reply_fragment(reg, 10, TS_REPLY_MULTIBYTE, NULL),
             RES_EINVAL);
}

static void multibyte_framing_refuses_what_it_should(void)
{
    uint8_t       out[300];
    size_t        len = 0;
    ts_mb_frame_t f;
    static const uint8_t data[] = { 1, 2 };

    /* A zero-width type has no data field to describe. */
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x50, 0),
             RES_EINVAL);
    CHECK_EQ(ts_build_mb_read(out, 4, &len, 0xE0, 0x20, 0x21, 2),
             RES_ETOOBIG);
    /* Past what a packet length field can express. */
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 250),
             RES_ETOOBIG);
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x21,
                              TS_FRAME_MAX_LEN - TS_MB_OVERHEAD), RES_OK);
    CHECK_EQ(len, TS_FRAME_MAX_LEN);
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x21,
                              TS_FRAME_MAX_LEN - TS_MB_OVERHEAD + 1u),
             RES_ETOOBIG);
    CHECK_EQ(ts_build_mb_read(NULL, sizeof(out), &len, 0xE0, 0x20, 0x21, 2),
             RES_EINVAL);

    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 1,
                               NULL, 2), RES_EINVAL);
    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 1,
                               data, 0), RES_EINVAL);

    /* A packet shorter than the header is not this format, whatever else it
     * might be -- including one that would have filled the old ten-byte
     * header exactly. */
    {
        uint8_t s[12] = { 0x0C, 0xE0, 0x00, 0x00, 0x20, 0x00,
                          0x00, 0x00, 0x00, 0x21, 0x00, 0x00 };

        s[11] = ts_checksum(s, 11);
        CHECK_EQ(ts_parse_mb_response(s, sizeof(s), &f), RES_EFORMAT);
    }

    /* And a corrupted one is refused rather than decoded. */
    {
        uint8_t s[TS_MB_OVERHEAD + 2u];
        size_t  n = 0;

        CHECK_EQ(ts_build_mb_write(s, sizeof(s), &n, 0xE0, 0x20, 0x21, 1,
                                   data, sizeof(data)), RES_OK);
        s[n - 1u] = (uint8_t)(s[n - 1u] ^ 0x01u);
        CHECK_EQ(ts_parse_mb_response(s, n, &f), RES_EFORMAT);
    }

    CHECK_EQ(ts_parse_mb_response(NULL, 15, &f), RES_EINVAL);
    CHECK_EQ(ts_parse_mb_response(out, 15, NULL), RES_EINVAL);
}

void test_frame_mb(void)
{
    a_multibyte_read_request_is_exact();
    a_multibyte_write_request_is_exact();
    a_multibyte_reply_is_read_at_the_tables_offsets();
    the_service_id_is_two_bytes_and_not_in_byte_2();
    reserved_fields_are_sent_as_zero_and_not_refused();
    the_fragment_number_is_found_wherever_it_lives();
    multibyte_framing_refuses_what_it_should();
}
