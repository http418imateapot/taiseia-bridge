/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Time is a parameter here, so every scheduling rule is tested by passing
 * numbers rather than by faking a clock.
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/model/appliance.h"
#include "core/taiseia/registration.h"
#include "ports/appliance_link.h"

static const uint8_t k_sup[] = { 0x00, 0x64 };

/* A dehumidifier with a writable mode and a read-only humidity reading. */
static void appliance(sim_link_t *s, link_t *l)
{
    sim_link_init(s, 0x04);
    CHECK_EQ(sim_link_set_identity(s, TS_FORMAT_GENERAL, TS_CLASS_APPLIANCE,
                                   0x0004, "ACME", "DH-200"), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x00, 0, true,  1,  k_sup, 2), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x03, 0, true,  50, k_sup, 2), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x07, 0, false, 62, k_sup, 2), RES_OK);
    sim_link_bind(s, l);
}

static void model(appliance_t *a)
{
    appl_policy_t p;

    appl_policy_defaults(&p);
    p.sweep_interval_ms      = 1000;
    p.reregister_interval_ms = 100000;
    p.max_misses             = 2;
    p.retry_backoff_min_ms   = 100;
    p.retry_backoff_max_ms   = 800;
    appl_init(a, &p);
}

/* Tick `n` times without letting the clock move, so scheduling stays explicit. */
static void pump(appliance_t *a, link_t *l, int64_t at_ms, int n)
{
    int i;

    for (i = 0; i < n; i++)
        CHECK_EQ(appl_tick(a, l, at_ms), RES_OK);
}

static void it_identifies_the_appliance_before_polling(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    const ts_registration_t *id;

    appliance(&s, &l);
    model(&a);

    CHECK_EQ(appl_state(&a), APPL_DOWN);
    CHECK(!appl_online(&a));
    CHECK(appl_identity(&a) == NULL);

    /* Submit, then collect. */
    pump(&a, &l, 0, 2);

    CHECK_EQ(appl_state(&a), APPL_READY);
    CHECK(appl_online(&a));

    id = appl_identity(&a);
    CHECK(id != NULL);
    if (id) {
        CHECK_EQ(id->sa_id, 0x0004);
        CHECK_STREQ(id->make, "ACME");
        CHECK_STREQ(id->model, "DH-200");
        CHECK_EQ(id->n_caps, 3);
    }
}

static void a_sweep_reads_every_capability(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    uint16_t    v = 0;

    appliance(&s, &l);
    model(&a);

    /* No value is available before anything has been read. */
    pump(&a, &l, 0, 2);
    CHECK_EQ(appl_value(&a, 0x07, &v), RES_ENOTFOUND);

    /* One submit and one collect per service, plus a tick to start the sweep. */
    pump(&a, &l, 0, 8);

    CHECK_EQ(appl_value(&a, 0x00, &v), RES_OK);
    CHECK_EQ(v, 1);
    CHECK_EQ(appl_value(&a, 0x03, &v), RES_OK);
    CHECK_EQ(v, 50);
    CHECK_EQ(appl_value(&a, 0x07, &v), RES_OK);
    CHECK_EQ(v, 62);

    /* A service the appliance never declared is not invented. */
    CHECK_EQ(appl_value(&a, 0x42, &v), RES_ENOTFOUND);
    CHECK_EQ(appl_value(NULL, 0x07, &v), RES_EINVAL);
    CHECK_EQ(appl_value(&a, 0x07, NULL), RES_EINVAL);
}

/*
 * The moment an appliance is identified nothing has been read from it yet, so
 * a caller that reports on sweep completion does not publish an empty
 * attribute set that looks like a device with nothing to say.
 */
static void a_sweep_is_only_counted_once_it_finishes(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;

    appliance(&s, &l);
    model(&a);

    pump(&a, &l, 0, 2);
    CHECK_EQ(appl_state(&a), APPL_READY);
    CHECK_EQ(appl_sweeps(&a), 0);          /* identified, nothing read */

    pump(&a, &l, 0, 3);                    /* sweep started, one service read */
    CHECK_EQ(appl_sweeps(&a), 0);

    pump(&a, &l, 0, 6);                    /* the rest of the pass */
    CHECK_EQ(appl_sweeps(&a), 1);

    /* And it does not tick over again until the next interval has run. */
    pump(&a, &l, 500, 6);
    CHECK_EQ(appl_sweeps(&a), 1);

    pump(&a, &l, 1000, 10);
    CHECK_EQ(appl_sweeps(&a), 2);

    CHECK_EQ(appl_sweeps(NULL), 0);
}

static void a_second_sweep_waits_for_its_interval(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 10);              /* registered and swept once */

    /* Nothing more is asked for until the interval elapses. */
    pump(&a, &l, 500, 6);
    CHECK_EQ(s.services[0].value, 1);

    /* Change the appliance behind the model's back; only a fresh sweep sees it. */
    s.services[2].value = 99;
    pump(&a, &l, 500, 6);
    {
        uint16_t v = 0;

        CHECK_EQ(appl_value(&a, 0x07, &v), RES_OK);
        CHECK_EQ(v, 62);

        pump(&a, &l, 1000, 10);
        CHECK_EQ(appl_value(&a, 0x07, &v), RES_OK);
        CHECK_EQ(v, 99);
    }
}

/*
 * Polling uses the general request format. A multi-byte appliance speaks a
 * different request layout that is not implemented, so it is identified and
 * then left alone -- reporting an empty attribute set without saying why would
 * look like a device with nothing to report.
 */
static void a_multibyte_appliance_is_identified_but_not_polled(void)
{
    sim_link_t    s;
    link_t        l;
    appliance_t   a;
    const uint8_t u16[] = { 0x00, 0x00, 0x27, 0x10 };
    uint16_t      v = 0;

    sim_link_init(&s, 0xE0);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0x00E0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x20, 0x21, false, 0, u16, 4), RES_OK);
    sim_link_bind(&s, &l);
    model(&a);

    pump(&a, &l, 0, 20);

    CHECK_EQ(appl_state(&a), APPL_READY_IDENTITY_ONLY);
    CHECK(appl_online(&a));            /* it is there, and we know what it is */
    CHECK(appl_identity(&a) != NULL);
    CHECK_EQ(appl_value(&a, 0x20, &v), RES_ENOTFOUND);
}

static void a_dropped_reply_is_retried(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    uint16_t    v = 0;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);
    CHECK_EQ(appl_state(&a), APPL_READY);

    /* Two of the three retries are consumed; the third attempt succeeds. */
    sim_link_drop_next(&s, 2);
    pump(&a, &l, 0, 12);

    CHECK_EQ(appl_value(&a, 0x00, &v), RES_OK);
    CHECK_EQ(appl_state(&a), APPL_READY);
}

/*
 * Values from a link that has stopped answering are not values. Publishing the
 * last known reading as though it were current is the failure this prevents.
 */
static void a_link_that_goes_down_invalidates_the_cache(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    uint16_t    v = 0;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 10);
    CHECK_EQ(appl_value(&a, 0x07, &v), RES_OK);

    sim_link_set_down(&s);
    pump(&a, &l, 0, 2);

    CHECK_EQ(appl_state(&a), APPL_DOWN);
    CHECK(!appl_online(&a));
    CHECK_EQ(appl_value(&a, 0x07, &v), RES_ENOTFOUND);
    CHECK(appl_identity(&a) == NULL);
}

static void repeated_failure_takes_the_appliance_down_and_backs_off(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;

    appliance(&s, &l);
    model(&a);

    /* Nothing ever answers, so registration cannot complete. */
    sim_link_drop_next(&s, 1000);
    pump(&a, &l, 0, 40);
    CHECK_EQ(appl_state(&a), APPL_DOWN);

    /* And it does not hammer the link: the next attempt waits for the backoff,
     * which grows on each failed cycle up to the configured ceiling. */
    CHECK(a.next_attempt_ms > 0);
    CHECK(a.backoff_ms <= 800);
    CHECK(a.backoff_ms >= 100);
}

/* An adapter that accepts a request and then never answers must not wedge the
 * model. This is the backstop, not the protocol's own deadline. */
typedef struct { unsigned submits; } dead_link_t;

static result_t dead_submit(void *self, const link_req_t *req)
{
    (void)req;
    ((dead_link_t *)self)->submits++;
    return RES_OK;
}

static result_t dead_poll(void *self, link_resp_t *out)
{
    (void)self;
    (void)out;
    return RES_EAGAIN;
}

static bool dead_is_up(void *self)
{
    (void)self;
    return true;
}

static void dead_close(void *self)
{
    (void)self;
}

static const link_vt_t k_dead_vt = {
    dead_submit, dead_poll, dead_is_up, dead_close
};

static void an_adapter_that_never_answers_does_not_wedge_the_model(void)
{
    dead_link_t dead;
    link_t      l;
    appliance_t a;

    dead.submits = 0;
    l.vt   = &k_dead_vt;
    l.self = &dead;
    model(&a);

    CHECK_EQ(appl_tick(&a, &l, 0), RES_OK);
    CHECK_EQ(dead.submits, 1);
    CHECK_EQ(appl_state(&a), APPL_IDENTIFYING);

    /* Still waiting, well before the backstop. */
    CHECK_EQ(appl_tick(&a, &l, APPL_STUCK_DEADLINE_MS - 1), RES_OK);
    CHECK_EQ(appl_state(&a), APPL_IDENTIFYING);

    CHECK_EQ(appl_tick(&a, &l, APPL_STUCK_DEADLINE_MS), RES_OK);
    CHECK_EQ(appl_state(&a), APPL_DOWN);

    CHECK_EQ(appl_tick(NULL, &l, 0), RES_EINVAL);
    CHECK_EQ(appl_tick(&a, NULL, 0), RES_EINVAL);
}

/* --------------------------------------------------------------- writes */

static void a_write_that_echoes_is_reported_written(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[1];
    const appl_write_batch_t *b;
    uint16_t    v = 0;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);

    items[0].service_id = 0x03;
    items[0].value      = 70;
    items[0].result     = APPL_WRITE_OK;
    items[0].done       = false;

    CHECK_EQ(appl_write_begin(&a, items, 1), RES_OK);
    CHECK(appl_write_active(&a));
    CHECK(!appl_write_done(&a));

    pump(&a, &l, 0, 4);

    CHECK(appl_write_done(&a));
    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b) {
        CHECK_EQ(b->n, 1);
        CHECK_EQ(b->items[0].result, APPL_WRITE_OK);
    }

    /* The appliance took it, and the cache knows without waiting for a sweep. */
    CHECK_EQ(s.services[1].value, 70);
    CHECK_EQ(appl_value(&a, 0x03, &v), RES_OK);
    CHECK_EQ(v, 70);

    appl_write_clear(&a);
    CHECK(!appl_write_active(&a));
    CHECK(appl_write_batch(&a) == NULL);
}

/*
 * The standard defines success as a reply identical to the request, and says
 * nothing about what a differing reply means. So this reports that the write
 * was not taken, and declines to guess why.
 */
static void a_write_whose_echo_differs_is_not_taken(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[1];
    const appl_write_batch_t *b;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);

    CHECK_EQ(sim_link_set_clamp(&s, 0x03, 60), RES_OK);
    CHECK_EQ(sim_link_set_clamp(&s, 0x42, 0), RES_ENOTFOUND);

    items[0].service_id = 0x03;
    items[0].value      = 70;
    items[0].result     = APPL_WRITE_OK;
    items[0].done       = false;

    CHECK_EQ(appl_write_begin(&a, items, 1), RES_OK);
    pump(&a, &l, 0, 4);

    CHECK(appl_write_done(&a));
    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b)
        CHECK_EQ(b->items[0].result, APPL_WRITE_NOT_TAKEN);

    CHECK_EQ(s.services[1].value, 60);
}

/* Both of these are answered from the capability set, with no round trip. */
static void writes_the_capability_set_can_refuse_cost_nothing(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[2];
    const appl_write_batch_t *b;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);

    items[0].service_id = 0x07;   /* declared read-only */
    items[0].value      = 1;
    items[0].result     = APPL_WRITE_OK;
    items[0].done       = false;
    items[1].service_id = 0x42;   /* never declared    */
    items[1].value      = 1;
    items[1].result     = APPL_WRITE_OK;
    items[1].done       = false;

    CHECK_EQ(appl_write_begin(&a, items, 2), RES_OK);

    /* Resolved before any tick has run. */
    CHECK(appl_write_done(&a));
    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b) {
        CHECK_EQ(b->items[0].result, APPL_WRITE_READ_ONLY);
        CHECK_EQ(b->items[1].result, APPL_WRITE_UNKNOWN);
    }

    /* The read-only service was not touched. */
    CHECK_EQ(s.services[2].value, 62);
}

static void write_batches_are_bounded_and_serialised(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[APPL_WRITE_BATCH_MAX + 1];
    size_t      i;

    for (i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        items[i].service_id = (uint8_t)i;
        items[i].value      = 1;
        items[i].result     = APPL_WRITE_OK;
        items[i].done       = false;
    }

    appliance(&s, &l);
    model(&a);

    /* Not until the appliance has been identified. */
    CHECK_EQ(appl_write_begin(&a, items, 1), RES_EIO);

    pump(&a, &l, 0, 2);

    CHECK_EQ(appl_write_begin(&a, items, 0), RES_ETOOBIG);
    CHECK_EQ(appl_write_begin(&a, items, APPL_WRITE_BATCH_MAX + 1u),
             RES_ETOOBIG);
    CHECK_EQ(appl_write_begin(&a, NULL, 1), RES_EINVAL);
    CHECK_EQ(appl_write_begin(NULL, items, 1), RES_EINVAL);

    CHECK_EQ(appl_write_begin(&a, items, 1), RES_OK);
    /* One command at a time; the caller clears a finished batch. */
    CHECK_EQ(appl_write_begin(&a, items, 1), RES_EBUSY);
}

/* A control command is what someone is waiting for; a sweep can take twenty
 * seconds. So the write goes first. */
static void a_write_preempts_a_sweep(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[1];

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);

    /* Start a sweep, then interrupt it after its first read. */
    pump(&a, &l, 0, 3);

    items[0].service_id = 0x03;
    items[0].value      = 70;
    items[0].result     = APPL_WRITE_OK;
    items[0].done       = false;
    CHECK_EQ(appl_write_begin(&a, items, 1), RES_OK);

    pump(&a, &l, 0, 2);
    CHECK(appl_write_done(&a));
    CHECK_EQ(s.services[1].value, 70);
}

/* A batch that cannot finish must say so, rather than leave a caller waiting
 * for a completion that is never coming. */
static void a_link_failure_completes_the_outstanding_batch(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    appl_write_item_t items[1];
    const appl_write_batch_t *b;

    appliance(&s, &l);
    model(&a);
    pump(&a, &l, 0, 2);

    items[0].service_id = 0x03;
    items[0].value      = 70;
    items[0].result     = APPL_WRITE_OK;
    items[0].done       = false;
    CHECK_EQ(appl_write_begin(&a, items, 1), RES_OK);

    sim_link_set_down(&s);
    pump(&a, &l, 0, 3);

    CHECK_EQ(appl_state(&a), APPL_DOWN);
    CHECK(appl_write_done(&a));
    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b)
        CHECK_EQ(b->items[0].result, APPL_WRITE_NO_REPLY);
}

static void the_names_cover_every_case(void)
{
    CHECK_STREQ(appl_state_str(APPL_DOWN), "down");
    CHECK_STREQ(appl_state_str(APPL_READY), "ready");
    CHECK_STREQ(appl_write_result_str(APPL_WRITE_OK), "written");
    CHECK_STREQ(appl_write_result_str(APPL_WRITE_NOT_TAKEN), "not taken");
}

void test_appliance(void)
{
    it_identifies_the_appliance_before_polling();
    a_sweep_reads_every_capability();
    a_sweep_is_only_counted_once_it_finishes();
    a_second_sweep_waits_for_its_interval();
    a_multibyte_appliance_is_identified_but_not_polled();
    a_dropped_reply_is_retried();
    a_link_that_goes_down_invalidates_the_cache();
    repeated_failure_takes_the_appliance_down_and_backs_off();
    an_adapter_that_never_answers_does_not_wedge_the_model();
    a_write_that_echoes_is_reported_written();
    a_write_whose_echo_differs_is_not_taken();
    writes_the_capability_set_can_refuse_cost_nothing();
    write_batches_are_bounded_and_serialised();
    a_write_preempts_a_sweep();
    a_link_failure_completes_the_outstanding_batch();
    the_names_cover_every_case();
}
