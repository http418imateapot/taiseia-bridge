/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/taiseia/datatype.h"
#include "core/taiseia/registration.h"
#include "ports/appliance_link.h"

/* Ask the simulator to register, and hand back the reply payload. */
static result_t registration_payload(sim_link_t *s, uint8_t *out, size_t cap,
                                     size_t *len)
{
    link_t      l;
    link_req_t  req;
    link_resp_t resp;
    result_t    r;

    sim_link_bind(s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 1; req.op = LINK_OP_REGISTER; req.sa_id = 0;
    req.service_id = 0; req.value = 0;

    r = link_submit(&l, &req);
    if (r != RES_OK)
        return r;
    r = link_poll(&l, &resp);
    if (r != RES_OK)
        return r;
    if (resp.status != RES_OK)
        return resp.status;
    if (resp.data_len > cap)
        return RES_ETOOBIG;

    memcpy(out, resp.data, resp.data_len);
    *len = resp.data_len;

    return RES_OK;
}

/*
 * The whole path, in bytes: configure an appliance, have the simulator
 * assemble a real reply, and decode it back. Nothing here compares a struct to
 * the struct it came from.
 */
static void a_general_format_reply_round_trips(void)
{
    sim_link_t        s;
    ts_registration_t reg;
    uint8_t           payload[TS_DATA_MAX];
    size_t            len = 0;
    const uint8_t     bits[]  = { 0x00, 0x03 };
    const uint8_t     range[] = { 0x10, 0x20 };
    const ts_capability_t *c;

    sim_link_init(&s, 0x04);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_GENERAL, TS_CLASS_APPLIANCE,
                                   0x0004, "ACME", "DH-200"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x00, 0, true,  1,  bits,  2), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x07, 0, false, 62, range, 2), RES_OK);

    CHECK_EQ(registration_payload(&s, payload, sizeof(payload), &len), RES_OK);
    CHECK_EQ(ts_registration_parse(payload, len, &reg), RES_OK);

    CHECK_EQ(reg.format, TS_FORMAT_GENERAL);
    CHECK_EQ(reg.device_class, TS_CLASS_APPLIANCE);
    CHECK_EQ(reg.fragment, 0);
    CHECK_EQ(ts_baud_from_code(reg.baud_code), 9600);
    CHECK_EQ(reg.sa_id, 0x0004);
    CHECK_STREQ(reg.make, "ACME");
    CHECK_STREQ(reg.model, "DH-200");
    CHECK_EQ(reg.n_caps, 2);

    c = ts_registration_find(&reg, 0x00);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->writable, 1);
        CHECK_EQ(c->support_len, 2);
        CHECK_EQ(c->support[1], 0x03);
    }

    c = ts_registration_find(&reg, 0x07);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->writable, 0);
        CHECK_EQ(c->support[0], 0x10);
        CHECK_EQ(c->support[1], 0x20);
    }

    CHECK(ts_registration_find(&reg, 0x42) == NULL);
    CHECK(ts_registration_find(NULL, 0x00) == NULL);
}

/*
 * The general format carries no type code, so two support bytes could be a
 * range, a maximum, or a bit mask and nothing on the wire says which. That
 * fact lives in the licensed service tables, which are not here. The decoder
 * must therefore report the bytes and decline to interpret them -- guessing
 * would produce a confident, wrong reading.
 */
static void a_general_format_capability_has_no_type_code(void)
{
    sim_link_t        s;
    ts_registration_t reg;
    uint8_t           payload[TS_DATA_MAX];
    size_t            len = 0;
    const uint8_t     sup[] = { 0x00, 0x64 };
    const ts_capability_t *c;

    sim_link_init(&s, 0x04);
    CHECK_EQ(sim_link_add_service(&s, 0x03, 0x21, false, 0, sup, 2), RES_OK);

    CHECK_EQ(registration_payload(&s, payload, sizeof(payload), &len), RES_OK);
    CHECK_EQ(ts_registration_parse(payload, len, &reg), RES_OK);

    c = ts_registration_find(&reg, 0x03);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->dtype_known, 0);
        CHECK_EQ(c->dtype, 0);
        CHECK_EQ(c->support_len, 2);
    }
}

static void a_multibyte_format_reply_carries_type_codes(void)
{
    sim_link_t        s;
    ts_registration_t reg;
    uint8_t           payload[TS_DATA_MAX];
    size_t            len = 0;
    /* UINT16 declares four support bytes: minimum then maximum. */
    const uint8_t     u16[] = { 0x00, 0x00, 0x27, 0x10 };
    /* ENUM16_Bit declares two: a mask of the functions it supports. */
    const uint8_t     bits[] = { 0x0F, 0x05 };
    const ts_capability_t *c;

    sim_link_init(&s, 0xE0);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0x00E0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x20, 0x21, false, 0, u16, 4), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x21, 0x10, true,  0, bits, 2), RES_OK);

    CHECK_EQ(registration_payload(&s, payload, sizeof(payload), &len), RES_OK);
    CHECK_EQ(ts_registration_parse(payload, len, &reg), RES_OK);

    CHECK_EQ(reg.format, TS_FORMAT_MULTIBYTE);
    CHECK_EQ(reg.device_class, TS_CLASS_SENSOR);
    CHECK_EQ(reg.sa_id, 0x00E0);
    CHECK_EQ(reg.n_caps, 2);

    c = ts_registration_find(&reg, 0x20);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->dtype_known, 1);
        CHECK_EQ(c->dtype, 0x21);
        CHECK_EQ(c->support_len, 4);
        CHECK_STREQ(ts_datatype_name(c->dtype), "UINT16");
    }

    c = ts_registration_find(&reg, 0x21);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->dtype, 0x10);
        CHECK_EQ(c->support_len, 2);
        CHECK_EQ((c->support[0] << 8) | c->support[1], 0x0F05);
    }
}

static void baud_codes_map_to_link_speeds(void)
{
    CHECK_EQ(ts_baud_from_code(0), 1200);
    CHECK_EQ(ts_baud_from_code(3), 9600);
    CHECK_EQ(ts_baud_from_code(6), 57600);
    CHECK_EQ(ts_baud_from_code(7), 0);
    CHECK_EQ(ts_baud_from_code(0xFF), 0);
}

/* A minimal well-formed payload, for the refusal tests to damage. */
static size_t make_payload(uint8_t *out, uint8_t class_byte, uint8_t speed_byte)
{
    size_t pos = 0;

    out[pos++] = class_byte;
    out[pos++] = 1;                 /* protocol major */
    out[pos++] = 0;                 /* protocol minor */
    out[pos++] = speed_byte;
    out[pos++] = 0x00;              /* SA id high */
    out[pos++] = 0x04;              /* SA id low  */
    out[pos++] = 'A'; out[pos++] = 0;
    out[pos++] = 'B'; out[pos++] = 0;

    return pos;
}

/*
 * A fragmented reply is one piece of a longer list. Reassembly is not
 * implemented, and decoding a piece on its own would report a truncated
 * capability list as though it were the whole thing.
 */
static void a_fragmented_reply_is_refused(void)
{
    ts_registration_t reg;
    uint8_t           p[64];
    size_t            len = make_payload(p, 0x00, 0x23);   /* fragment 2 */

    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EUNSUPPORTED);

    /* The header is still filled in, so a caller can say why it gave up. */
    CHECK_EQ(reg.fragment, 2);
    CHECK_EQ(ts_baud_from_code(reg.baud_code), 9600);
}

static void a_malformed_payload_is_refused(void)
{
    ts_registration_t reg;
    uint8_t           p[64];
    size_t            len;
    size_t            i;

    /* Shorter than the fixed header. */
    (void)make_payload(p, 0x00, 0x03);
    for (i = 0; i < TS_REG_HEADER_LEN; i++)
        CHECK_EQ(ts_registration_parse(p, i, &reg), RES_EFORMAT);

    /* A string with no terminator: the model never ends. */
    len = 6;
    p[len++] = 'A'; p[len++] = 0;
    for (i = 0; i < 40u; i++)
        p[len++] = 'B';
    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EFORMAT);

    /* A record stream that does not divide evenly -- two bytes left over
     * where a three-byte record should start. */
    len = make_payload(p, 0x00, 0x03);
    p[len++] = 0x01; p[len++] = 0x00;
    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EFORMAT);

    CHECK_EQ(ts_registration_parse(NULL, 10, &reg), RES_EINVAL);
    CHECK_EQ(ts_registration_parse(p, 10, NULL), RES_EINVAL);
}

/*
 * Without the type we cannot know how wide the record is, so every record
 * after it would be read at the wrong offset. Refusing the whole reply is the
 * only honest option; resynchronising by guessing is not.
 */
static void an_unknown_type_code_stops_the_stream(void)
{
    ts_registration_t reg;
    uint8_t           p[64];
    size_t            len = make_payload(p, 0x80, 0x03);   /* multi-byte */

    p[len++] = 0x20;                /* service 0x20 */
    p[len++] = 0xAA;                /* no such type code */
    p[len++] = 0x00; p[len++] = 0x00;

    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EUNSUPPORTED);

    /* The string type has no fixed support width, so it lands the same way. */
    len = make_payload(p, 0x80, 0x03);
    p[len++] = 0x20;
    p[len++] = 0x50;                /* STRING */
    p[len++] = 0x08;
    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EUNSUPPORTED);
}

/*
 * More services than we can hold is refused outright. Truncating would report
 * a device as unable to do things it can do, and would look entirely normal.
 */
static void too_many_services_is_refused(void)
{
    ts_registration_t reg;
    uint8_t           p[TS_DATA_MAX];
    size_t            len = make_payload(p, 0x00, 0x03);
    unsigned          i;

    for (i = 0; i < TS_MAX_CAPABILITIES; i++) {
        p[len++] = (uint8_t)i;
        p[len++] = 0x00;
        p[len++] = 0x01;
    }
    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_OK);
    CHECK_EQ(reg.n_caps, TS_MAX_CAPABILITIES);

    p[len++] = 0x7F;
    p[len++] = 0x00;
    p[len++] = 0x01;
    CHECK_EQ(ts_registration_parse(p, len, &reg), RES_EFORMAT);
}

/*
 * The support buffer is sized by hand from the widest entry in the type table.
 * If a type is ever added that needs more, this is what says so.
 */
static void the_support_buffer_fits_every_known_type(void)
{
    size_t i;

    for (i = 0; i < ts_datatype_count(); i++) {
        const ts_datatype_t *t = ts_datatype_at(i);

        CHECK(t != NULL);
        if (t)
            CHECK(t->support_len <= TS_SUPPORT_MAX_BYTES);
    }
}

void test_registration(void)
{
    a_general_format_reply_round_trips();
    a_general_format_capability_has_no_type_code();
    a_multibyte_format_reply_carries_type_codes();
    baud_codes_map_to_link_speeds();
    a_fragmented_reply_is_refused();
    a_malformed_payload_is_refused();
    an_unknown_type_code_stops_the_stream();
    too_many_services_is_refused();
    the_support_buffer_fits_every_known_type();
}
