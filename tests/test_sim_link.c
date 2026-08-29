/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/taiseia/datatype.h"
#include "ports/appliance_link.h"

/* An appliance with one read-only and one writable service. */
static void build(sim_link_t *s, link_t *l)
{
    sim_link_init(s, 0x04);
    CHECK_EQ(sim_link_add_service(s, 0x00, 0x01, true,  1, 0, 0x0003), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x07, 0x21, false, 62, 0, 100),   RES_OK);
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

    req.id = 1; req.op = LINK_OP_WRITE; req.sa_id = 0x04;
    req.service_id = 0x00; req.value = 3;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.write, 1);
    CHECK_EQ((resp.data[0] << 8) | resp.data[1], 3);

    /* Reading it back must show the value that was written. */
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

    req.id = 3; req.op = LINK_OP_WRITE; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 99;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EUNSUPPORTED);

    /* And the value must be untouched. */
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

    req.id = 6; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ETIMEOUT);

    req.id = 7;
    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_ETIMEOUT);

    /* The third goes through. */
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

    req.id = 9; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_EFORMAT);
    CHECK_EQ(resp.id, 9);

    /* Corruption is one-shot; the next exchange is clean. */
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

    req.id = 30; req.op = LINK_OP_READ; req.sa_id = 0x04;
    req.service_id = 0x07; req.value = 0;

    CHECK_EQ(link_submit(&l, &req), RES_EIO);
    CHECK_EQ(link_poll(&l, &resp), RES_EAGAIN);
}

static void registration_returns_the_configured_bytes(void)
{
    sim_link_t    s;
    link_t        l;
    link_req_t    req;
    link_resp_t   resp;
    const uint8_t blob[] = { 0x04, 0x01, 0x00, 0x21 };

    build(&s, &l);
    CHECK_EQ(sim_link_set_registration(&s, blob, sizeof(blob)), RES_OK);

    req.id = 40; req.op = LINK_OP_REGISTER; req.sa_id = 0x04;
    req.service_id = 0x00; req.value = 0;

    CHECK_EQ(exchange(&l, &req, &resp), RES_OK);
    CHECK_EQ(resp.status, RES_OK);
    CHECK_EQ(resp.data_len, sizeof(blob));
    CHECK_EQ(resp.data[0], 0x04);
    CHECK_EQ(resp.data[3], 0x21);
}

static void capacity_and_argument_limits_hold(void)
{
    sim_link_t s;
    unsigned   i;

    sim_link_init(&s, 0x01);

    for (i = 0; i < TS_MAX_CAPABILITIES; i++)
        CHECK_EQ(sim_link_add_service(&s, (uint8_t)i, 0x21, false,
                                      0, 0, 0), RES_OK);

    CHECK_EQ(sim_link_add_service(&s, 0x7F, 0x21, false, 0, 0, 0),
             RES_ETOOBIG);

    sim_link_init(&s, 0x01);
    CHECK_EQ(sim_link_add_service(&s, 0x80, 0x21, false, 0, 0, 0), RES_EINVAL);
    CHECK_EQ(sim_link_add_service(&s, 0x01, 0x21, false, 0, 0, 0), RES_OK);
    /* A duplicate service id would make lookups ambiguous. */
    CHECK_EQ(sim_link_add_service(&s, 0x01, 0x21, false, 0, 0, 0), RES_EINVAL);
    CHECK_EQ(sim_link_add_service(NULL, 0x02, 0x21, false, 0, 0, 0),
             RES_EINVAL);
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
    registration_returns_the_configured_bytes();
    capacity_and_argument_limits_hold();
    the_dispatcher_tolerates_a_null_link();
    closing_releases_the_link();
}
