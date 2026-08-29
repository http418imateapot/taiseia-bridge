/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/model/appliance.h"
#include "core/mqtt/config.h"
#include "core/mqtt/payload.h"
#include "core/mqtt/topic.h"

/* ----------------------------------------------------------------- topics */

static void templates_substitute_both_placeholders(void)
{
    char out[TOPIC_MAX];

    CHECK_EQ(topic_render(out, sizeof(out),
                          "{prefix}/from/device/{id}/telemetry",
                          "v1", "TS-1"), RES_OK);
    CHECK_STREQ(out, "v1/from/device/TS-1/telemetry");

    /* A template need not use either. */
    CHECK_EQ(topic_render(out, sizeof(out), "fixed/topic", "v1", "TS-1"),
             RES_OK);
    CHECK_STREQ(out, "fixed/topic");

    CHECK_EQ(topic_render(out, sizeof(out), "{id}{id}", "v1", "A"), RES_OK);
    CHECK_STREQ(out, "AA");
}

/*
 * An unknown placeholder would publish to a topic nobody subscribes to, and
 * nothing downstream would ever report the silence. Refusing at startup is the
 * only moment anyone is looking.
 */
static void an_unknown_placeholder_is_refused(void)
{
    char out[TOPIC_MAX];

    CHECK_EQ(topic_render(out, sizeof(out), "{prefix}/{tenant}/x", "v1", "A"),
             RES_EFORMAT);
    CHECK_EQ(topic_render(out, sizeof(out), "{prefix", "v1", "A"),
             RES_EFORMAT);
    CHECK_EQ(topic_render(out, sizeof(out), "{}", "v1", "A"), RES_EFORMAT);
    CHECK_EQ(topic_render(out, sizeof(out), "", "v1", "A"), RES_EFORMAT);

    CHECK_EQ(topic_render(out, 8, "{prefix}/from/device/{id}/telemetry",
                          "v1", "TS-1"), RES_ETOOBIG);

    CHECK_EQ(topic_render(NULL, 8, "x", "v1", "A"), RES_EINVAL);
    CHECK_EQ(topic_render(out, 0, "x", "v1", "A"), RES_EINVAL);
    CHECK_EQ(topic_render(out, sizeof(out), NULL, "v1", "A"), RES_EINVAL);
}

/*
 * Two publishers on one topic makes a consumer see alternating payload shapes,
 * which reads as data corruption rather than as the configuration mistake it
 * is. Cheaper to catch here.
 */
static void a_duplicate_topic_is_caught_at_startup(void)
{
    const char *ok[] = {
        "{prefix}/a/{id}", "{prefix}/b/{id}", "{prefix}/c/{id}"
    };
    const char *dup[] = {
        "{prefix}/a/{id}", "{prefix}/b/{id}", "{prefix}/a/{id}"
    };
    const char *bad[] = {
        "{prefix}/a/{id}", "{oops}/b"
    };
    size_t which = 99;

    CHECK_EQ(topic_check_set(ok, 3, "v1", "A", &which), RES_OK);

    CHECK_EQ(topic_check_set(dup, 3, "v1", "A", &which), RES_EINVAL);
    CHECK_EQ(which, 2);

    CHECK_EQ(topic_check_set(bad, 2, "v1", "A", &which), RES_EFORMAT);
    CHECK_EQ(which, 1);

    CHECK_EQ(topic_check_set(ok, 0, "v1", "A", &which), RES_ETOOBIG);
    CHECK_EQ(topic_check_set(NULL, 3, "v1", "A", &which), RES_EINVAL);
}

/* --------------------------------------------------------------- payloads */

static const uint8_t k_bits[]  = { 0x00, 0x03 };
static const uint8_t k_range[] = { 0x00, 0x64 };

static void ready_appliance(sim_link_t *s, link_t *l, appliance_t *a)
{
    appl_policy_t p;

    sim_link_init(s, 0x04);
    CHECK_EQ(sim_link_set_identity(s, TS_FORMAT_GENERAL, TS_CLASS_APPLIANCE,
                                   0x0004, "ACME", "DH-200"), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x00, 0, true,  1,  k_bits,  2), RES_OK);
    CHECK_EQ(sim_link_add_service(s, 0x07, 0, false, 62, k_range, 2), RES_OK);
    sim_link_bind(s, l);

    appl_policy_defaults(&p);
    p.sweep_interval_ms = 1000;
    appl_init(a, &p);

    {
        int i;

        for (i = 0; i < 12; i++)
            CHECK_EQ(appl_tick(a, l, 0), RES_OK);
    }
}

static void telemetry_carries_raw_values(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    char        buf[1024];
    strbuf_t    sb;

    ready_appliance(&s, &l, &a);
    sb_init(&sb, buf, sizeof(buf));

    CHECK_EQ(payload_telemetry(&sb, "TS-1", "2026-08-29T00:00:00.000Z", &a),
             RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\","
                "\"device_time\":\"2026-08-29T00:00:00.000Z\","
                "\"sa_id\":4,\"online\":true,"
                "\"attributes\":{\"0\":1,\"7\":62}}");
}

/*
 * Going silent would leave a consumer unable to tell a detached appliance from
 * a dead bridge, so an offline report still goes out -- with an empty
 * attribute set and nothing pretending to be current.
 */
static void an_offline_report_is_still_a_report(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    char        buf[1024];
    strbuf_t    sb;

    ready_appliance(&s, &l, &a);
    sim_link_set_down(&s);
    CHECK_EQ(appl_tick(&a, &l, 0), RES_OK);

    sb_init(&sb, buf, sizeof(buf));
    CHECK_EQ(payload_telemetry(&sb, "TS-1", "T", &a), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"sa_id\":null,\"online\":false,\"attributes\":{}}");
}

/*
 * The general record format carries no data type code, so the two support
 * bytes are handed over as they arrived. Naming a shape here would be a guess
 * dressed as a fact.
 */
static void general_format_capabilities_are_raw(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    char        buf[2048];
    strbuf_t    sb;

    ready_appliance(&s, &l, &a);
    sb_init(&sb, buf, sizeof(buf));

    CHECK_EQ(payload_capabilities(&sb, "TS-1", "T", &a), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"sa_id\":4,\"online\":true,\"format\":\"general\","
                "\"make\":\"ACME\",\"model\":\"DH-200\",\"capabilities\":["
                "{\"id\":0,\"access\":\"rw\","
                "\"support\":{\"kind\":\"raw\",\"hex\":\"0003\"}},"
                "{\"id\":7,\"access\":\"r\","
                "\"support\":{\"kind\":\"raw\",\"hex\":\"0064\"}}]}");
}

/* With a type code the shape is knowable, and is resolved rather than dumped. */
static void multibyte_capabilities_resolve_their_shape(void)
{
    sim_link_t    s;
    link_t        l;
    appliance_t   a;
    appl_policy_t p;
    char          buf[2048];
    strbuf_t      sb;
    const uint8_t u16[]  = { 0x00, 0x10, 0x00, 0x64 };   /* min 16, max 100 */
    const uint8_t mask[] = { 0x0F, 0x05 };
    const uint8_t stamp[] = { 0x07, 0xE8, 0x0C, 0x0C, 0x0E, 0x1E };
    int           i;

    sim_link_init(&s, 0xE0);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0x00E0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x01, 0x21, true, 0, u16, 4), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x02, 0x10, false, 0, mask, 2), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x03, 0x44, false, 0, stamp, 6), RES_OK);
    sim_link_bind(&s, &l);

    appl_policy_defaults(&p);
    appl_init(&a, &p);
    for (i = 0; i < 6; i++)
        CHECK_EQ(appl_tick(&a, &l, 0), RES_OK);
    CHECK_EQ(appl_state(&a), APPL_READY_IDENTITY_ONLY);

    sb_init(&sb, buf, sizeof(buf));
    CHECK_EQ(payload_capabilities(&sb, "TS-1", "T", &a), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"sa_id\":224,\"online\":true,\"format\":\"multibyte\","
                "\"make\":\"ACME\",\"model\":\"PM-1\",\"capabilities\":["
                "{\"id\":1,\"access\":\"rw\",\"dtype\":\"UINT16\","
                "\"support\":{\"kind\":\"range\",\"min\":16,\"max\":100}},"
                "{\"id\":2,\"access\":\"r\",\"dtype\":\"ENUM16_Bit\","
                "\"support\":{\"kind\":\"bits\",\"mask\":3845,\"width\":16}},"
                "{\"id\":3,\"access\":\"r\",\"dtype\":\"Y-M-D-H-M-S\","
                "\"support\":{\"kind\":\"raw\",\"hex\":\"07e80c0c0e1e\"}}]}");
}

static void status_and_the_will_differ_only_in_the_timestamp(void)
{
    char     buf[256];
    strbuf_t sb;

    sb_init(&sb, buf, sizeof(buf));
    CHECK_EQ(payload_status(&sb, "TS-1", "T", true), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"online\":true}");

    /* The broker publishes the will at an unknown later moment; a timestamp
     * taken now would misstate when the device went away. */
    sb_reset(&sb);
    CHECK_EQ(payload_status(&sb, "TS-1", NULL, false), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"device_id\":\"TS-1\",\"device_time\":null,"
                "\"online\":false}");
}

static void an_acknowledgement_echoes_the_tag_and_names_each_outcome(void)
{
    char              buf[512];
    strbuf_t          sb;
    appl_write_batch_t batch;
    write_code_map_t  map;

    memset(&batch, 0, sizeof(batch));
    batch.n = 3;
    batch.items[0].service_id = 3;
    batch.items[0].result     = APPL_WRITE_OK;
    batch.items[1].service_id = 7;
    batch.items[1].result     = APPL_WRITE_READ_ONLY;
    batch.items[2].service_id = 9;
    batch.items[2].result     = APPL_WRITE_NOT_TAKEN;

    write_code_map_defaults(&map);
    sb_init(&sb, buf, sizeof(buf));

    CHECK_EQ(payload_ack(&sb, "TS-1", "T", "{\"k\":1}", 7, &batch, &map),
             RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"success\":false,\"tag\":{\"k\":1},"
                "\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"results\":{\"3\":\"ok\",\"7\":\"read_only\","
                "\"9\":\"not_taken\"}}");

    /* A deployment whose backend wants numbers configures them; they are a
     * property of that deployment, not of this code. */
    map.numeric = true;
    map.code[APPL_WRITE_OK]        = 0;
    map.code[APPL_WRITE_READ_ONLY] = 255;
    map.code[APPL_WRITE_NOT_TAKEN] = 240;
    sb_reset(&sb);
    CHECK_EQ(payload_ack(&sb, "TS-1", "T", "1", 1, &batch, &map), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"success\":false,\"tag\":1,"
                "\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"results\":{\"3\":0,\"7\":255,\"9\":240}}");

    /* All written is the only success. */
    batch.n = 1;
    sb_reset(&sb);
    CHECK_EQ(payload_ack(&sb, "TS-1", "T", NULL, 0, &batch, NULL), RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"success\":true,\"tag\":null,"
                "\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"results\":{\"3\":\"ok\"}}");

    sb_reset(&sb);
    CHECK_EQ(payload_ack_error(&sb, "TS-1", "T", "1", 1, "malformed command"),
             RES_OK);
    CHECK_STREQ(sb_str(&sb),
                "{\"success\":false,\"tag\":1,"
                "\"device_id\":\"TS-1\",\"device_time\":\"T\","
                "\"error\":\"malformed command\"}");
}

/* A payload that did not fit must be reported, never published truncated. */
static void a_payload_that_does_not_fit_is_refused(void)
{
    sim_link_t  s;
    link_t      l;
    appliance_t a;
    char        small[32];
    strbuf_t    sb;

    ready_appliance(&s, &l, &a);
    sb_init(&sb, small, sizeof(small));

    CHECK_EQ(payload_telemetry(&sb, "TS-1", "T", &a), RES_ETOOBIG);
    CHECK_EQ(payload_capabilities(&sb, "TS-1", "T", &a), RES_ETOOBIG);

    CHECK_EQ(payload_telemetry(NULL, "TS-1", "T", &a), RES_EINVAL);
    CHECK_EQ(payload_telemetry(&sb, NULL, "T", &a), RES_EINVAL);
    CHECK_EQ(payload_telemetry(&sb, "TS-1", "T", NULL), RES_EINVAL);
    CHECK_EQ(payload_status(&sb, NULL, "T", true), RES_EINVAL);
}

void test_payload(void)
{
    templates_substitute_both_placeholders();
    an_unknown_placeholder_is_refused();
    a_duplicate_topic_is_caught_at_startup();
    telemetry_carries_raw_values();
    an_offline_report_is_still_a_report();
    general_format_capabilities_are_raw();
    multibyte_capabilities_resolve_their_shape();
    status_and_the_will_differ_only_in_the_timestamp();
    an_acknowledgement_echoes_the_tag_and_names_each_outcome();
    a_payload_that_does_not_fit_is_refused();
}
