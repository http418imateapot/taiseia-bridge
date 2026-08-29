/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/taiseia/datatype.h"
#include "ports/appliance_link.h"

/* Support data as it appears on the wire: two bytes in the general format. */
static const uint8_t k_sup_bits[]  = { 0x00, 0x03 };   /* modes 0 and 1     */
static const uint8_t k_sup_range[] = { 0x00, 0x64 };   /* minimum 0, max 100 */

/* An appliance with one writable and one read-only service. */
static void build(sim_link_t *s, link_t *l)
{
    sim_link_init(s, 0x04);
    CHECK_EQ(sim_link_add_service(s, 0x00, 0x01, true, 1,
                                  k_sup_bits, sizeof(k_sup_bits)), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x07, 0x21, false, 62,
                                  k_sup_range, sizeof(k_sup_range)), RES_OK);
    sim_link_bind(s, l);
}

static result_t exchange(link_t *l, const link_req_t *req, link_resp_t *resp)
{
    result_t r = link_submit(l, req);

    if (r != RES_OK)
        return r;

    return link_poll(l, resp);
}

static void a_read_returns_the_current_value(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);
    CHECK(link_is_up(&l));

    memset(&req, 0, sizeof(req));
    req.id = 11; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.id, 11);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.sa_id, 0x04);
    CHECK_EQ(resp.service_id, 0x07);
    CHECK_EQ(resp.write, 0);
    CHECK_EQ(resp.data_len, 2);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 62);
}

static void a_write_takes_effect_and_is_echoed(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 1; req.op = LINK_OP_WRITE; req.sa_id = 0x04;
    req.service_id = 0x00; req.value = 3;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.write, 1);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 3);

    /* Reading it back must show the value that was written. */
    memset(&req, 0, sizeof(req));
    req.id = 2; req.op = LINK_OP_READ; req.value = 0;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 3);
}

static void writing_a_read_only_service_is_refused(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 3; req.op = LINK_OP_WRITE; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 99;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EUNSUPPORTED);

    /* And the value must be untouched. */
    memset(&req, 0, sizeof(req));
    req.id = 4; req.op = LINK_OP_READ; req.value = 0;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 62);
}

static void an_unknown_service_is_reported_not_found(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 5; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x42; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ENOTFOUND);
    CHECK_EQ(resp.id, 5);
}

static void a_dropped_request_times_out(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);
    sim_link_drop_next(&s, 2);

    memset(&req, 0, sizeof(req));
    req.id = 6; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ETIMEOUT);

    memset(&req, 0, sizeof(req));
    req.id = 7;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ETIMEOUT);

    /* The third goes through. */
    memset(&req, 0, sizeof(req));
    req.id = 8;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
}

static void a_corrupted_reply_is_reported_as_malformed(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);
    sim_link_corrupt_next(&s);

    memset(&req, 0, sizeof(req));
    req.id = 9; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EFORMAT);
    CHECK_EQ(resp.id, 9);

    /* Corruption is one-shot; the next exchange is clean. */
    memset(&req, 0, sizeof(req));
    req.id = 10;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
}

static void a_second_request_before_the_first_is_collected_is_refused(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 20; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(link_submit(&l, &req), RES_OK);
    CHECK_EQ(link_submit(&l, &req), RES_EBUSY);
    CHECK_EQ(link_poll(&l, &resp), RES_OK);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
    CHECK_EQ(link_submit(&l, &req), RES_OK);
}

static void a_down_link_refuses_work(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);
    sim_link_set_down(&s);

    CHECK(!link_is_up(&l));

    memset(&req, 0, sizeof(req));
    req.id = 30; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(link_submit(&l, &req), RES_EIO);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
}

/*
 * A registration reply carries SA id 0x00 in the frame -- the appliance type
 * lives inside the payload instead. Decoding that payload is
 * test_registration.c; here we only establish that the frame is shaped right.
 */
static void a_registration_reply_is_framed_as_one(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;
    link_resp_t resp;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 40; req.op = LINK_OP_REGISTER; req.sa_id = 0x04;
    req.service_id = 0x00; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.sa_id, 0x00);
    CHECK(resp.data_len > TS_REG_HEADER_LEN);
    CHECK_EQ(resp.data[0], TS_CLASS_APPLIANCE);   /* general format */
}

static void a_raw_registration_overrides_the_assembled_one(void)
{
    sim_link_t    s;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    const uint8_t blob[] = { 0xAA, 0xBB, 0xCC, 0xDD };

    build(&s, &l);
    CHECK_EQ(sim_link_set_raw_registration(&s, blob, sizeof(blob)), RES_OK);

    memset(&req, 0, sizeof(req));
    req.id = 41; req.op = LINK_OP_REGISTER; req.sa_id = 0x04;
    req.service_id = 0x00; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.data_len, sizeof(blob));
    CHECK_EQ(resp.data[0], 0xAA);
    CHECK_EQ(resp.data[3], 0xDD);
}

/*
 * The multi-byte layout end to end through the simulator: a read that asks for
 * whatever the appliance has and gets a batch, and a write that comes back
 * echoed. Both go out as bytes and come back through the parser.
 */
static void a_multibyte_exchange_carries_wide_values(void)
{
    sim_link_t    s;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    const uint8_t u32[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x42, 0x40 };
    uint8_t       item[4];

    sim_link_init(&s, 0xE0);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0x00E0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x20, 0x22, true, 1000, u32, 8),
             RES_OK);
    CHECK_EQ(sim_link_set_items(&s, 0x20, 3), RES_OK);
    sim_link_bind(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 60; req.op = LINK_OP_READ; req.sa_id = 0xE0;
    req.service_id = 0x20;
    req.multibyte = true; req.dtype = 0x22; req.value_len = 4;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.multibyte, 1);
    CHECK_EQ(resp.write, 0);
    CHECK_EQ(resp.dtype, 0x22);
    CHECK_EQ(resp.count, 3);
    CHECK_EQ(resp.data_len, 12);
    /* 1000, then 1001, then 1002: the batch is not three copies of one
     * number, so a mis-sliced read cannot pass. */
    CHECK_EQ(resp.data[3], 0xE8);
    CHECK_EQ(resp.data[7], 0xE9);
    CHECK_EQ(resp.data[11], 0xEA);

    item[0] = 0x00; item[1] = 0x00; item[2] = 0x04; item[3] = 0xD2;
    memset(&req, 0, sizeof(req));
    req.id = 61; req.op = LINK_OP_WRITE; req.sa_id = 0xE0;
    req.service_id = 0x20;
    req.multibyte = true; req.dtype = 0x22; req.value_len = 4;
    req.count = 1; req.data = item; req.data_len = sizeof(item);

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.multibyte, 1);
    CHECK_EQ(resp.write, 1);
    CHECK_EQ(resp.data_len, 4);
    CHECK(memcmp(resp.data, item, sizeof(item)) == 0);
    CHECK_EQ(s.services[0].value, 1234);
}

/* A request that says "multi-byte" without a usable type is refused, not sent.
 * This is what an uninitialised request looks like from the inside. */
static void a_multibyte_request_without_a_type_is_refused(void)
{
    sim_link_t  s;
    link_t      l;
    link_req_t  req;

    build(&s, &l);

    memset(&req, 0, sizeof(req));
    req.id = 62; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07;
    req.multibyte = true; req.dtype = 0xFE; req.value_len = 0;

    CHECK_EQ(link_submit(&l, &req), RES_EINVAL);
}

static void capacity_and_argument_limits_hold(void)
{
    sim_link_t s;
    unsigned   i;

    sim_link_init(&s, 0x01);

    for (i = 0; i < TS_MAX_CAPABILITIES; i++)
        CHECK_EQ(sim_link_add_service(&s, (uint8_t)i, 0x21, false, 0,
                                      k_sup_range, 2), RES_OK);

    CHECK_EQ(sim_link_add_service(&s, 0x7F, 0x21, false, 0, k_sup_range, 2),
             RES_ETOOBIG);

    sim_link_init(&s, 0x01);
    CHECK_EQ(sim_link_add_service(&s, 0x80, 0x21, false, 0, k_sup_range, 2),
             RES_EINVAL);
    CHECK_EQ(sim_link_add_service(&s, 0x01, 0x21, false, 0, k_sup_range, 2),
             RES_OK);
    /* A duplicate service id would make lookups ambiguous. */
    CHECK_EQ(sim_link_add_service(&s, 0x01, 0x21, false, 0, k_sup_range, 2),
             RES_EINVAL);
    CHECK_EQ(sim_link_add_service(NULL, 0x02, 0x21, false, 0, k_sup_range, 2),
             RES_EINVAL);
    CHECK_EQ(sim_link_add_service(&s, 0x02, 0x21, false, 0, NULL, 2),
             RES_EINVAL);

    /* The general format has no type code, so its support datum is always
     * two bytes. Anything else could not have come off a wire. */
    CHECK_EQ(sim_link_add_service(&s, 0x03, 0x21, false, 0, k_sup_range, 1),
             RES_EINVAL);

    /* In the multi-byte format the width has to match what the type implies,
     * or the decoder would read the next record at the wrong offset. */
    sim_link_init(&s, 0x01);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0xE0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x04, 0x21, false, 0, k_sup_range, 2),
             RES_EINVAL);                            /* UINT16 wants four */
}

static void the_dispatcher_tolerates_a_null_link(void)
{
    link_req_t  req;
    link_resp_t resp;
    link_t      empty;

    /* Nothing here should read either struct, but leaving them indeterminate
     * makes that an assumption rather than a fact. */
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    empty.vt   = NULL;
    empty.self = NULL;

    CHECK_EQ(link_submit(NULL, &req), RES_EINVAL);
    CHECK_EQ(link_poll(NULL, &resp), RES_EINVAL);
    CHECK_EQ(link_submit(&empty, &req), RES_EINVAL);
    CHECK_EQ(link_poll(&empty, &resp), RES_EINVAL);
    CHECK(!link_is_up(NULL));
    CHECK(!link_is_up(&empty));

    link_close(NULL);
    link_close(&empty);
}

static void closing_releases_the_link(void)
{
    sim_link_t s;
    link_t     l;

    build(&s, &l);
    CHECK(link_is_up(&l));

    link_close(&l);
    CHECK(!link_is_up(&l));
    CHECK(l.vt == NULL);

    /* Closing twice is not an error. */
    link_close(&l);
}

void test_sim_link(void)
{
    a_read_returns_the_current_value();
    a_write_takes_effect_and_is_echoed();
    writing_a_read_only_service_is_refused();
    an_unknown_service_is_reported_not_found();
    a_dropped_request_times_out();
    a_corrupted_reply_is_reported_as_malformed();
    a_second_request_before_the_first_is_collected_is_refused();
    a_down_link_refuses_work();
    a_registration_reply_is_framed_as_one();
    a_raw_registration_overrides_the_assembled_one();
    a_multibyte_exchange_carries_wide_values();
    a_multibyte_request_without_a_type_is_refused();
    capacity_and_argument_limits_hold();
    the_dispatcher_tolerates_a_null_link();
    closing_releases_the_link();
}
