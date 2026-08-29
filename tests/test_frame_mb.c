/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multi-byte framing, byte for byte against the standard's own table.
 *
 * Every field in this header is a place to be one byte out, and every such
 * mistake decodes into something that still looks like data.
 */
#include "tests.h"

#include "core/taiseia/frame.h"

static void a_multibyte_read_request_is_exact(void)
{
    uint8_t out[32];
    size_t  len = 0;
    static const uint8_t want[] = {
        0x0E,                    /* length: 10 overhead + 4 data          */
        0xE0,                    /* SA id                                  */
        0x20,                    /* read, service 0x20                     */
        0xFF,                    /* reserved                               */
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

static void a_multibyte_write_request_carries_its_items(void)
{
    uint8_t       out[32];
    size_t        len = 0;
    ts_mb_frame_t f;
    static const uint8_t data[] = { 0x00, 0x10, 0x00, 0x20 };

    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x21, 0x21, 2,
                               data, sizeof(data)), RES_OK);
    CHECK_EQ(len, TS_MB_OVERHEAD + 4u);
    CHECK_EQ(out[2], 0x80 | 0x21);
    CHECK_EQ(out[3], 0xFF);
    CHECK_EQ(out[6], 0x21);
    CHECK_EQ(out[7], 0x00);
    CHECK_EQ(out[8], 0x02);

    CHECK_EQ(ts_parse_mb_response(out, len, &f), RES_OK);
    CHECK_EQ(f.sa_id, 0xE0);
    CHECK_EQ(f.service_id, 0x21);
    CHECK_EQ(f.write, 1);
    CHECK_EQ(f.reserved, 0xFF);
    CHECK_EQ(f.fragment, 0);
    CHECK_EQ(f.dtype, 0x21);
    CHECK_EQ(f.count, 2);
    CHECK_EQ(f.data_len, 4);
    CHECK_EQ(f.data[1], 0x10);
}

static void a_multibyte_reply_reports_its_count_and_fragment(void)
{
    uint8_t       pkt[16];
    ts_mb_frame_t f;
    uint16_t      frag = 0;

    pkt[0]  = 0x0C;              /* 10 overhead + 2 data */
    pkt[1]  = 0xE0;
    pkt[2]  = 0x20;
    pkt[3]  = 0xFF;
    pkt[4]  = 0x00;
    pkt[5]  = 0x03;              /* fragment 3 */
    pkt[6]  = 0x21;
    pkt[7]  = 0x00;
    pkt[8]  = 0x01;              /* one item */
    pkt[9]  = 0x12;
    pkt[10] = 0x34;
    pkt[11] = ts_checksum(pkt, 11);

    CHECK_EQ(ts_parse_mb_response(pkt, 12, &f), RES_OK);
    CHECK_EQ(f.fragment, 3);
    CHECK_EQ(f.count, 1);
    CHECK_EQ(f.data_len, 2);
    CHECK_EQ((f.data[0] << 8) | f.data[1], 0x1234);

    /* And the fragment can be had without decoding the rest, which is what
     * the transport needs while it is still gathering pieces. */
    CHECK_EQ(ts_reply_fragment(pkt, 12, TS_REPLY_MULTIBYTE, &frag), RES_OK);
    CHECK_EQ(frag, 3);
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
    CHECK_EQ(ts_reply_fragment(reg, 4, TS_REPLY_MULTIBYTE, &frag),
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

    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x80, 0x21, 2),
             RES_EINVAL);
    /* A zero-width type has no data field to describe. */
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x50, 0),
             RES_EINVAL);
    CHECK_EQ(ts_build_mb_read(out, 4, &len, 0xE0, 0x20, 0x21, 2),
             RES_ETOOBIG);
    /* Past what a packet length field can express. */
    CHECK_EQ(ts_build_mb_read(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 250),
             RES_ETOOBIG);
    CHECK_EQ(ts_build_mb_read(NULL, sizeof(out), &len, 0xE0, 0x20, 0x21, 2),
             RES_EINVAL);

    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 1,
                               NULL, 2), RES_EINVAL);
    CHECK_EQ(ts_build_mb_write(out, sizeof(out), &len, 0xE0, 0x20, 0x21, 1,
                               data, 0), RES_EINVAL);

    /* A packet shorter than the header is not this format, whatever else it
     * might be. */
    {
        uint8_t s[6] = { 0x06, 0xE0, 0x20, 0x00, 0x00, 0x00 };

        s[5] = ts_checksum(s, 5);
        CHECK_EQ(ts_parse_mb_response(s, 6, &f), RES_EFORMAT);
    }

    /* And a corrupted one is refused rather than decoded. */
    {
        uint8_t s[12];
        size_t  n = 0;

        CHECK_EQ(ts_build_mb_write(s, sizeof(s), &n, 0xE0, 0x20, 0x21, 1,
                                   data, sizeof(data)), RES_OK);
        s[n - 1u] = (uint8_t)(s[n - 1u] ^ 0x01u);
        CHECK_EQ(ts_parse_mb_response(s, n, &f), RES_EFORMAT);
    }

    CHECK_EQ(ts_parse_mb_response(NULL, 12, &f), RES_EINVAL);
    CHECK_EQ(ts_parse_mb_response(out, 12, NULL), RES_EINVAL);
}

void test_frame_mb(void)
{
    a_multibyte_read_request_is_exact();
    a_multibyte_write_request_carries_its_items();
    a_multibyte_reply_reports_its_count_and_fragment();
    the_fragment_number_is_found_wherever_it_lives();
    multibyte_framing_refuses_what_it_should();
}
