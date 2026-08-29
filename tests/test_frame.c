/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "core/taiseia/frame.h"

static void checksum_is_xor_of_everything_before_it(void)
{
    const uint8_t buf[] = { 0x06, 0x04, 0x03, 0xFF, 0xFF };

    CHECK_EQ(ts_checksum(buf, sizeof(buf)),
             0x06 ^ 0x04 ^ 0x03 ^ 0xFF ^ 0xFF);

    CHECK_EQ(ts_checksum(buf, 0), 0);
    CHECK_EQ(ts_checksum(NULL, 4), 0);
}

static void read_request_is_six_bytes_with_ff_filler(void)
{
    uint8_t  out[8];
    size_t   len = 0;

    CHECK_EQ(ts_build_read(out, sizeof(out), &len, 0x04, 0x07), RES_OK);
    CHECK_EQ(len, 6);
    CHECK_EQ(out[0], 6);          /* length includes itself and the checksum */
    CHECK_EQ(out[1], 0x04);       /* SA id                                   */
    CHECK_EQ(out[2], 0x07);       /* service id, write bit clear             */
    CHECK_EQ(out[3], 0xFF);
    CHECK_EQ(out[4], 0xFF);
    CHECK_EQ(out[5], ts_checksum(out, 5));
}

static void write_request_sets_the_write_bit_and_packs_big_endian(void)
{
    uint8_t out[8];
    size_t  len = 0;

    CHECK_EQ(ts_build_write(out, sizeof(out), &len, 0x01, 0x03, 0x1234),
             RES_OK);
    CHECK_EQ(len, 6);
    CHECK_EQ(out[2], 0x80 | 0x03);
    CHECK_EQ(out[3], 0x12);
    CHECK_EQ(out[4], 0x34);
    CHECK_EQ(out[5], ts_checksum(out, 5));
}

static void builders_reject_bad_arguments(void)
{
    uint8_t out[8];
    size_t  len = 0;

    /* A service id cannot occupy the write bit. */
    CHECK_EQ(ts_build_read(out, sizeof(out), &len, 0x04, 0x80), RES_EINVAL);
    CHECK_EQ(ts_build_write(out, sizeof(out), &len, 0x04, 0xFF, 0), RES_EINVAL);

    CHECK_EQ(ts_build_read(out, 5, &len, 0x04, 0x07), RES_ETOOBIG);
    CHECK_EQ(ts_build_read(NULL, sizeof(out), &len, 0x04, 0x07), RES_EINVAL);
    CHECK_EQ(ts_build_read(out, sizeof(out), NULL, 0x04, 0x07), RES_EINVAL);
}

static void requests_round_trip_through_the_parser(void)
{
    uint8_t    out[8];
    size_t     len = 0;
    ts_frame_t f;

    CHECK_EQ(ts_build_write(out, sizeof(out), &len, 0x04, 0x03, 0x0046),
             RES_OK);
    CHECK_EQ(ts_parse_request(out, len, &f), RES_OK);
    CHECK_EQ(f.sa_id, 0x04);
    CHECK_EQ(f.service_id, 0x03);
    CHECK_EQ(f.write, 1);
    CHECK_EQ(f.data_len, 2);
    CHECK_EQ(f.data[0], 0x00);
    CHECK_EQ(f.data[1], 0x46);
}

static void a_wrong_checksum_is_rejected(void)
{
    uint8_t    pkt[6] = { 0x06, 0x04, 0x07, 0x00, 0x2A, 0x00 };
    ts_frame_t f;

    pkt[5] = ts_checksum(pkt, 5);
    CHECK_EQ(ts_parse_request(pkt, sizeof(pkt), &f), RES_OK);

    pkt[5] = (uint8_t)(pkt[5] ^ 0x01u);
    CHECK_EQ(ts_parse_request(pkt, sizeof(pkt), &f), RES_EFORMAT);
}

static void a_length_that_disagrees_with_the_buffer_is_rejected(void)
{
    uint8_t    pkt[8] = { 0x08, 0x04, 0x07, 0x00, 0x2A, 0x00, 0x00, 0x00 };
    ts_frame_t f;

    pkt[7] = ts_checksum(pkt, 7);

    /* Well formed at its declared length. */
    CHECK_EQ(ts_parse_request(pkt, 8, &f), RES_OK);
    CHECK_EQ(f.data_len, 4);

    /* Claiming eight bytes while holding six must not read past the end;
     * claiming eight while holding ten must not leave two unaccounted for. */
    CHECK_EQ(ts_parse_request(pkt, 6, &f), RES_EFORMAT);
    CHECK_EQ(ts_parse_request(pkt, 7, &f), RES_EFORMAT);
}

static void reserved_lengths_are_refused(void)
{
    uint8_t    pkt[8];
    ts_frame_t f;
    size_t     need = 0;
    unsigned   n;

    /* 0..5 and 255 are reserved and must never be treated as a size. */
    for (n = 0; n < 6u; n++) {
        pkt[0] = (uint8_t)n;
        CHECK_EQ(ts_frame_declared_len(pkt, 1, &need), RES_EFORMAT);
    }

    pkt[0] = 0xFF;
    CHECK_EQ(ts_frame_declared_len(pkt, 1, &need), RES_EFORMAT);

    pkt[0] = 0xFE;
    CHECK_EQ(ts_frame_declared_len(pkt, 1, &need), RES_OK);
    CHECK_EQ(need, 254);

    pkt[0] = 0x06;
    CHECK_EQ(ts_frame_declared_len(pkt, 1, &need), RES_OK);
    CHECK_EQ(need, 6);

    /* One byte is enough to learn the size; none is not. */
    CHECK_EQ(ts_frame_declared_len(pkt, 0, &need), RES_EAGAIN);
    CHECK_EQ(ts_frame_declared_len(NULL, 1, &need), RES_EINVAL);
    CHECK_EQ(ts_frame_declared_len(pkt, 1, NULL), RES_EINVAL);

    CHECK_EQ(ts_parse_request(pkt, 0, &f), RES_EAGAIN);
    CHECK_EQ(ts_parse_request(NULL, 6, &f), RES_EINVAL);
    CHECK_EQ(ts_parse_request(pkt, 6, NULL), RES_EINVAL);
}

static void a_maximum_length_packet_parses(void)
{
    uint8_t    pkt[254];
    ts_frame_t f;
    size_t     i;

    pkt[0] = 254;
    pkt[1] = 0xE0;
    pkt[2] = 0x11;
    for (i = 3; i < 253; i++)
        pkt[i] = (uint8_t)i;
    pkt[253] = ts_checksum(pkt, 253);

    CHECK_EQ(ts_parse_request(pkt, sizeof(pkt), &f), RES_OK);
    CHECK_EQ(f.data_len, 254 - 4);
    CHECK_EQ(f.data[0], 3);
}

/*
 * The one place the two directions disagree. A registration reply has no
 * service byte, so its payload starts at byte 2 and is one byte longer than
 * the request layout would report. Parse it the wrong way and every field
 * inside the payload shifts by one -- which is a decoding error that produces
 * plausible-looking output, the worst kind.
 */
static void a_registration_reply_has_no_service_byte(void)
{
    uint8_t    pkt[7] = { 0x07, 0x00, 0x80, 0x01, 0x00, 0x03, 0x00 };
    ts_frame_t f;

    pkt[6] = ts_checksum(pkt, 6);

    CHECK_EQ(ts_parse_response(pkt, sizeof(pkt), &f), RES_OK);
    CHECK_EQ(f.registration, 1);
    CHECK_EQ(f.sa_id, 0x00);
    CHECK_EQ(f.data_len, 4);
    CHECK_EQ(f.data[0], 0x80);      /* the payload's own first byte */
    CHECK_EQ(f.data[3], 0x03);

    /* Read as a request, the same bytes lose one and mislabel the rest. */
    CHECK_EQ(ts_parse_request(pkt, sizeof(pkt), &f), RES_OK);
    CHECK_EQ(f.registration, 0);
    CHECK_EQ(f.data_len, 3);
    CHECK_EQ(f.data[0], 0x01);
}

static void a_non_registration_reply_keeps_its_service_byte(void)
{
    uint8_t    pkt[6] = { 0x06, 0x04, 0x87, 0x00, 0x2A, 0x00 };
    ts_frame_t f;

    pkt[5] = ts_checksum(pkt, 5);

    CHECK_EQ(ts_parse_response(pkt, sizeof(pkt), &f), RES_OK);
    CHECK_EQ(f.registration, 0);
    CHECK_EQ(f.sa_id, 0x04);
    CHECK_EQ(f.service_id, 0x07);
    CHECK_EQ(f.write, 1);
    CHECK_EQ(f.data_len, 2);
    CHECK_EQ(f.data[1], 0x2A);
}

static void the_response_parser_refuses_the_same_things(void)
{
    uint8_t    pkt[6] = { 0x06, 0x00, 0x01, 0x02, 0x03, 0x00 };
    ts_frame_t f;

    pkt[5] = ts_checksum(pkt, 5);
    CHECK_EQ(ts_parse_response(pkt, sizeof(pkt), &f), RES_OK);

    CHECK_EQ(ts_parse_response(NULL, 6, &f), RES_EINVAL);
    CHECK_EQ(ts_parse_response(pkt, 6, NULL), RES_EINVAL);
    CHECK_EQ(ts_parse_response(pkt, 0, &f), RES_EAGAIN);

    /* Shorter than any well-formed packet, so the SA id byte it would need to
     * classify the layout is not there to read. */
    CHECK_EQ(ts_parse_response(pkt, 1, &f), RES_EFORMAT);
    CHECK_EQ(ts_parse_response(pkt, 5, &f), RES_EFORMAT);

    pkt[5] = (uint8_t)(pkt[5] ^ 0x40u);
    CHECK_EQ(ts_parse_response(pkt, sizeof(pkt), &f), RES_EFORMAT);
}

void test_frame(void)
{
    checksum_is_xor_of_everything_before_it();
    read_request_is_six_bytes_with_ff_filler();
    write_request_sets_the_write_bit_and_packs_big_endian();
    builders_reject_bad_arguments();
    requests_round_trip_through_the_parser();
    a_wrong_checksum_is_rejected();
    a_length_that_disagrees_with_the_buffer_is_rejected();
    reserved_lengths_are_refused();
    a_maximum_length_packet_parses();
    a_registration_reply_has_no_service_byte();
    a_non_registration_reply_keeps_its_service_byte();
    the_response_parser_refuses_the_same_things();
}
