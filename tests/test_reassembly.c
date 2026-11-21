/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rejoining a reply that arrived in pieces.
 *
 * The failure this guards against is not a crash. It is a reply that has been
 * silently joined out of the wrong pieces -- a lost fragment, two exchanges
 * overlapping, a continuation read at the wrong offset -- and still decodes
 * into a number that someone will act on. So most of what follows is about
 * what the reassembler refuses.
 */
#include "tests.h"

#include "core/taiseia/reassembly.h"
#include "core/taiseia/registration.h"

/*
 * One multi-byte fragment: `n` items of one byte each, all valued `fill`.
 * Written by hand at CNS 16014:2025 table 15's offsets rather than through the
 * builder, so that a builder and a parser that agree on a wrong layout cannot
 * pass here together.
 */
static size_t mb_fragment(uint8_t *out, uint16_t fragment, uint16_t service,
                          uint8_t dtype, uint16_t count, uint8_t fill)
{
    size_t len = 13u + count;
    size_t i;

    out[0]  = (uint8_t)len;
    out[1]  = 0xE0;
    out[2]  = 0x00;                                 /* service type: read */
    out[3]  = (uint8_t)((service >> 8) & 0xFFu);
    out[4]  = (uint8_t)(service & 0xFFu);
    out[5]  = 0x00;                                 /* reserved           */
    out[6]  = 0x00;
    out[7]  = (uint8_t)((fragment >> 8) & 0xFFu);
    out[8]  = (uint8_t)(fragment & 0xFFu);
    out[9]  = dtype;
    out[10] = (uint8_t)((count >> 8) & 0xFFu);
    out[11] = (uint8_t)(count & 0xFFu);
    for (i = 0; i < count; i++)
        out[12u + i] = fill;
    out[len - 1u] = ts_checksum(out, len - 1u);

    return len;
}

/*
 * One registration fragment. The first carries the six-byte header and then
 * `n` bytes of payload; a continuation repeats the header and carries `n`
 * bytes after it -- which is the inference the module documents, and the only
 * layout in which the fragment number could be found where it is.
 */
static size_t reg_fragment(uint8_t *out, uint16_t fragment, uint8_t fill,
                           size_t n)
{
    size_t len = TS_REG_FRAME_OVERHEAD + TS_REG_HEADER_LEN + n;
    size_t i;

    out[0] = (uint8_t)len;
    out[1] = (uint8_t)TS_SA_ID_REGISTRATION;
    out[2] = 0x01;                                  /* general format       */
    out[3] = 0x01;
    out[4] = 0x00;
    out[5] = (uint8_t)((fragment << 4) | 0x04u);    /* fragment | baud code */
    out[6] = 0x00;
    out[7] = 0x10;
    for (i = 0; i < n; i++)
        out[TS_REG_FRAME_OVERHEAD - 1u + TS_REG_HEADER_LEN + i] = fill;
    out[len - 1u] = ts_checksum(out, len - 1u);

    return len;
}

static void a_single_fragment_is_already_whole(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n = mb_fragment(pkt, 0, 0x20, 0x20, 3, 0xAA);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_OK);
    CHECK(ts_reasm_complete(&r));
    CHECK_EQ(r.fragments, 1);
    CHECK_EQ(r.len, 3);
    CHECK_EQ(r.count, 3);
    CHECK_EQ(r.service_id, 0x20);
    CHECK_EQ(r.dtype, 0x20);
}

static void multibyte_fragments_join_in_order(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);

    n = mb_fragment(pkt, 2, 0x20, 0x20, 4, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);
    CHECK(!ts_reasm_complete(&r));

    n = mb_fragment(pkt, 1, 0x20, 0x20, 4, 0x22);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    n = mb_fragment(pkt, 0, 0x20, 0x20, 2, 0x33);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_OK);

    CHECK(ts_reasm_complete(&r));
    CHECK_EQ(r.fragments, 3);
    CHECK_EQ(r.len, 10);
    /* Each fragment reports what it carried; the total is the sum. */
    CHECK_EQ(r.count, 10);
    CHECK_EQ(r.data[0], 0x11);
    CHECK_EQ(r.data[3], 0x11);
    CHECK_EQ(r.data[4], 0x22);
    CHECK_EQ(r.data[7], 0x22);
    CHECK_EQ(r.data[8], 0x33);
    CHECK_EQ(r.data[9], 0x33);
}

/* A gap in the numbering means a fragment was lost. The bytes on either side
 * of it would join into something that parses, which is the danger. */
static void a_skipped_fragment_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);

    n = mb_fragment(pkt, 3, 0x20, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    n = mb_fragment(pkt, 1, 0x20, 0x20, 2, 0x22);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);
}

static void a_repeated_fragment_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n = mb_fragment(pkt, 2, 0x20, 0x20, 2, 0x11);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);
}

/*
 * Two exchanges overlapping. Nothing about the fragment numbers gives this
 * away -- 1 then 0 counts down perfectly -- so the service and type are what
 * has to catch it.
 */
static void interleaved_replies_are_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);

    n = mb_fragment(pkt, 1, 0x20, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    n = mb_fragment(pkt, 0, 0x21, 0x20, 2, 0x22);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);

    n = mb_fragment(pkt, 1, 0x20, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    n = mb_fragment(pkt, 0, 0x20, 0x22, 2, 0x22);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);
}

/*
 * The format gives the service id two bytes; the link port holds seven bits.
 * 0x0120 masked to seven bits is 0x20, a real service, so masking would hand
 * one service's readings over as another's. It is refused instead.
 */
static void a_service_id_wider_than_the_port_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    n = mb_fragment(pkt, 0, 0x0120, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EUNSUPPORTED);
    CHECK(!ts_reasm_complete(&r));

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    n = mb_fragment(pkt, 0, 0x0080, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EUNSUPPORTED);

    /* The widest id the port can carry still passes. */
    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    n = mb_fragment(pkt, 0, 0x007F, 0x20, 2, 0x11);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_OK);
    CHECK_EQ(r.service_id, 0x7F);
}

/* Past the buffer, the answer is a refusal. A truncated batch of readings
 * looks exactly like a complete one. */
static void an_oversize_reply_is_refused_not_truncated(void)
{
    ts_reasm_t r;
    uint8_t    pkt[TS_FRAME_MAX_LEN];
    size_t     n;
    uint16_t   frag;
    unsigned   accepted = 0;
    result_t   last = RES_OK;

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);

    /* Enough full fragments to overrun the buffer whatever its size. */
    for (frag = (uint16_t)(TS_REASSEMBLY_MAX / 200u + 4u); frag > 0; frag--) {
        n = mb_fragment(pkt, frag, 0x20, 0x20, 200, 0x55);
        last = ts_reasm_add(&r, pkt, n);
        if (last != RES_EAGAIN)
            break;
        accepted++;
    }

    CHECK_EQ(last, RES_ETOOBIG);
    CHECK(!ts_reasm_complete(&r));
    CHECK(r.len <= TS_REASSEMBLY_MAX);
    CHECK(accepted > 0);
}

static void a_damaged_fragment_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n = mb_fragment(pkt, 0, 0x20, 0x20, 3, 0xAA);

    pkt[n - 1u] = (uint8_t)(pkt[n - 1u] ^ 0xFFu);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);
    CHECK(!ts_reasm_complete(&r));
}

static void registration_fragments_drop_the_repeated_header(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_REGISTRATION);

    n = reg_fragment(pkt, 1, 0xAA, 4);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    n = reg_fragment(pkt, 0, 0xBB, 3);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_OK);

    CHECK(ts_reasm_complete(&r));
    CHECK_EQ(r.fragments, 2);
    /* Header once, then both payloads: 6 + 4 + 3. */
    CHECK_EQ(r.len, 13);
    CHECK_EQ(r.data[0], 0x01);          /* the header, kept from the first  */
    /* Whole, so the fragment nibble is cleared and the baud code survives. */
    CHECK_EQ(r.data[3], 0x04);
    CHECK_EQ(r.data[5], 0x10);
    CHECK_EQ(r.data[6], 0xAA);
    CHECK_EQ(r.data[9], 0xAA);
    CHECK_EQ(r.data[10], 0xBB);
    CHECK_EQ(r.data[12], 0xBB);
}

/*
 * The point of all of it: a capability list too long for one packet, joined
 * up and then decoded. The strings are split across the boundary on purpose,
 * because that is where an off-by-one in the join would hide.
 */
static void a_fragmented_registration_decodes_as_one(void)
{
    ts_reasm_t        r;
    ts_registration_t reg;
    uint8_t           pkt[64];
    size_t            len;
    size_t            pos;

    ts_reasm_begin(&r, TS_REPLY_REGISTRATION);

    /* Fragment 1: the header, the make, and half the model. */
    pos = 2;
    pkt[pos++] = 0x01;                  /* general format, class 1 */
    pkt[pos++] = 0x01;
    pkt[pos++] = 0x00;
    pkt[pos++] = (uint8_t)((1u << 4) | 0x04u);
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x04;                  /* SA id 4 */
    pkt[pos++] = (uint8_t)'A'; pkt[pos++] = (uint8_t)'C';
    pkt[pos++] = (uint8_t)'M'; pkt[pos++] = (uint8_t)'E';
    pkt[pos++] = 0u;                    /* end of make */
    pkt[pos++] = (uint8_t)'D'; pkt[pos++] = (uint8_t)'H';
    len = pos + 1u;
    pkt[0] = (uint8_t)len;
    pkt[1] = (uint8_t)TS_SA_ID_REGISTRATION;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);
    CHECK_EQ(ts_reasm_add(&r, pkt, len), RES_EAGAIN);

    /* Fragment 0: the header again, the rest of the model, two records. */
    pos = 2;
    pkt[pos++] = 0x01;
    pkt[pos++] = 0x01;
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x04;                  /* fragment 0 */
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x04;
    pkt[pos++] = (uint8_t)'-'; pkt[pos++] = (uint8_t)'2';
    pkt[pos++] = (uint8_t)'0'; pkt[pos++] = (uint8_t)'0';
    pkt[pos++] = 0u;                    /* end of model */
    pkt[pos++] = 0x80 | 0x03;           /* service 3, writable */
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x64;
    pkt[pos++] = 0x07;                  /* service 7, read only */
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x64;
    len = pos + 1u;
    pkt[0] = (uint8_t)len;
    pkt[1] = (uint8_t)TS_SA_ID_REGISTRATION;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);
    CHECK_EQ(ts_reasm_add(&r, pkt, len), RES_OK);

    CHECK(ts_reasm_complete(&r));
    CHECK_EQ(ts_registration_parse(r.data, r.len, &reg), RES_OK);
    CHECK_EQ(reg.format, TS_FORMAT_GENERAL);
    CHECK_EQ(reg.sa_id, 4);
    CHECK_EQ(reg.baud_code, 4);
    CHECK_EQ(reg.fragment, 0);
    CHECK_STREQ(reg.make, "ACME");
    CHECK_STREQ(reg.model, "DH-200");
    CHECK_EQ(reg.n_caps, 2);
    CHECK_EQ(reg.caps[0].service_id, 3);
    CHECK_EQ(reg.caps[0].writable, 1);
    CHECK_EQ(reg.caps[1].service_id, 7);
    CHECK_EQ(reg.caps[1].writable, 0);
}

/* A continuation shorter than the header it must repeat cannot be a
 * continuation, whatever its fragment number says. */
static void a_headerless_continuation_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n;

    ts_reasm_begin(&r, TS_REPLY_REGISTRATION);

    n = reg_fragment(pkt, 1, 0xAA, 4);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EAGAIN);

    /* Five payload bytes: byte 5 still lands where the fragment number is
     * read, and still says zero, but there is no whole header to skip. */
    pkt[0] = 8;
    pkt[1] = (uint8_t)TS_SA_ID_REGISTRATION;
    pkt[2] = 0x01;
    pkt[3] = 0x01;
    pkt[4] = 0x00;
    pkt[5] = 0x04;                      /* fragment 0, baud code 4 */
    pkt[6] = 0x00;
    pkt[7] = ts_checksum(pkt, 7);
    n = 8;
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EFORMAT);
}

/* A general reply has nowhere to put a fragment number, so it is always
 * whole -- and reassembly has to be transparent for it. */
static void a_general_reply_passes_straight_through(void)
{
    ts_reasm_t r;
    uint8_t    pkt[8];

    pkt[0] = 6;
    pkt[1] = 0xE0;
    pkt[2] = 0x20;
    pkt[3] = 0x01;
    pkt[4] = 0x2C;
    pkt[5] = ts_checksum(pkt, 5);

    ts_reasm_begin(&r, TS_REPLY_GENERAL);
    CHECK_EQ(ts_reasm_add(&r, pkt, 6), RES_OK);
    CHECK(ts_reasm_complete(&r));
    CHECK_EQ(r.fragments, 1);
    CHECK_EQ(r.len, 2);
    CHECK_EQ(r.service_id, 0x20);
    CHECK_EQ(r.write, 0);
    CHECK_EQ(r.data[0], 0x01);
    CHECK_EQ(r.data[1], 0x2C);
}

static void adding_after_completion_is_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n = mb_fragment(pkt, 0, 0x20, 0x20, 2, 0xAA);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_OK);
    CHECK_EQ(ts_reasm_add(&r, pkt, n), RES_EBUSY);
}

static void nulls_are_refused(void)
{
    ts_reasm_t r;
    uint8_t    pkt[64];
    size_t     n = mb_fragment(pkt, 0, 0x20, 0x20, 2, 0xAA);

    ts_reasm_begin(&r, TS_REPLY_MULTIBYTE);
    CHECK_EQ(ts_reasm_add(&r, NULL, n), RES_EINVAL);
    CHECK_EQ(ts_reasm_add(NULL, pkt, n), RES_EINVAL);
    CHECK(!ts_reasm_complete(NULL));

    ts_reasm_begin(NULL, TS_REPLY_MULTIBYTE);   /* must not crash */
}

void test_reassembly(void)
{
    a_single_fragment_is_already_whole();
    multibyte_fragments_join_in_order();
    a_skipped_fragment_is_refused();
    a_repeated_fragment_is_refused();
    interleaved_replies_are_refused();
    a_service_id_wider_than_the_port_is_refused();
    an_oversize_reply_is_refused_not_truncated();
    a_damaged_fragment_is_refused();
    registration_fragments_drop_the_repeated_header();
    a_fragmented_registration_decodes_as_one();
    a_headerless_continuation_is_refused();
    a_general_reply_passes_straight_through();
    adding_after_completion_is_refused();
    nulls_are_refused();
}
