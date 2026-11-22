/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is where a stranger's bytes become instructions for an appliance, so
 * most of what is worth testing here is what it refuses.
 */
#include "tests.h"

#include "adapters/sim/sim_link.h"
#include "core/mqtt/control.h"
#include "core/mqtt/payload.h"
#include "core/util/strbuf.h"

static result_t parse(const char *s, control_cmd_t *out)
{
    return control_parse(s, strlen(s), out);
}

static void a_command_becomes_a_batch(void)
{
    control_cmd_t c;

    CHECK_EQ(parse("{\"tag\":1,\"attributes\":{\"3\":70,\"0\":1}}", &c),
             RES_OK);
    CHECK_EQ(c.n, 2);
    CHECK_EQ(c.items[0].service_id, 3);
    CHECK_EQ(c.items[0].value, 70);
    CHECK_EQ(c.items[0].done, 0);
    CHECK_EQ(c.items[1].service_id, 0);
    CHECK_EQ(c.items[1].value, 1);
    CHECK(c.has_tag);
    CHECK_EQ(c.tag_len, 1);
    CHECK_EQ(c.tag[0], '1');
}

/*
 * The tag's JSON type is the sender's business. Copying the bytes is what lets
 * the acknowledgement come back equal to what they sent, rather than to what a
 * round trip through a decoder made of it.
 */
static void a_tag_of_any_type_is_copied_raw(void)
{
    static const char *cases[] = {
        "{\"tag\":\"abc\",\"attributes\":{\"1\":1}}",
        "{\"tag\":{\"k\":[1,2]},\"attributes\":{\"1\":1}}",
        "{\"tag\":[1,null],\"attributes\":{\"1\":1}}",
        "{\"tag\":-7,\"attributes\":{\"1\":1}}",
        "{\"tag\":null,\"attributes\":{\"1\":1}}"
    };
    static const char *want[] = {
        "\"abc\"", "{\"k\":[1,2]}", "[1,null]", "-7", "null"
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        control_cmd_t c;
        char          got[64];

        CHECK_EQ(parse(cases[i], &c), RES_OK);
        CHECK(c.has_tag);
        CHECK(c.tag_len < sizeof(got));
        memcpy(got, c.tag, c.tag_len);
        got[c.tag_len] = '\0';
        CHECK_STREQ(got, want[i]);
    }
}

static void a_command_without_a_tag_is_still_a_command(void)
{
    control_cmd_t c;

    CHECK_EQ(parse("{\"attributes\":{\"7\":9}}", &c), RES_OK);
    CHECK(!c.has_tag);
    CHECK_EQ(c.tag_len, 0);
    CHECK_EQ(c.n, 1);
}

/*
 * A command that asks for nothing is not a command. Accepting it would produce
 * an acknowledgement with an empty result set, which reads as success.
 */
static void a_command_that_asks_for_nothing_is_refused(void)
{
    control_cmd_t c;

    CHECK_EQ(parse("{\"attributes\":{}}", &c), RES_EFORMAT);
    CHECK_EQ(parse("{\"tag\":1}", &c), RES_EFORMAT);
    CHECK_EQ(parse("{\"attributes\":[1,2]}", &c), RES_EFORMAT);
    CHECK_EQ(parse("{}", &c), RES_EFORMAT);
}

/*
 * A key we half-understand would address a service the sender did not name.
 * Only the plain decimal form is accepted.
 */
static void a_key_that_is_not_a_service_id_is_refused(void)
{
    static const char *bad[] = {
        "{\"attributes\":{\"03\":1}}",      /* leading zero        */
        "{\"attributes\":{\" 3\":1}}",      /* space               */
        "{\"attributes\":{\"3 \":1}}",
        "{\"attributes\":{\"0x3\":1}}",
        "{\"attributes\":{\"abc\":1}}",
        "{\"attributes\":{\"\":1}}",
        "{\"attributes\":{\"65536\":1}}",   /* past sixteen bits   */
        "{\"attributes\":{\"100000\":1}}",
        "{\"attributes\":{\"00288\":1}}"    /* zeros to five digits */
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        control_cmd_t c;

        CHECK_EQ(parse(bad[i], &c), RES_EFORMAT);
    }

    /* The boundary itself is fine. */
    {
        control_cmd_t c;

        CHECK_EQ(parse("{\"attributes\":{\"65535\":1}}", &c), RES_OK);
        CHECK_EQ(c.items[0].service_id, 65535);
        CHECK_EQ(parse("{\"attributes\":{\"0\":1}}", &c), RES_OK);
        CHECK_EQ(c.items[0].service_id, 0);
    }
}

static void a_value_that_will_not_fit_is_refused(void)
{
    static const char *bad[] = {
        "{\"attributes\":{\"1\":-1}}",
        "{\"attributes\":{\"1\":65536}}",
        "{\"attributes\":{\"1\":\"70\"}}",
        "{\"attributes\":{\"1\":true}}",
        "{\"attributes\":{\"1\":null}}",
        "{\"attributes\":{\"1\":1.5}}"
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        control_cmd_t c;

        CHECK_EQ(parse(bad[i], &c), RES_EFORMAT);
    }

    {
        control_cmd_t c;

        CHECK_EQ(parse("{\"attributes\":{\"1\":65535}}", &c), RES_OK);
        CHECK_EQ(c.items[0].value, 65535);
    }
}

static void more_than_a_batch_holds_is_refused(void)
{
    control_cmd_t c;
    char          js[512];
    size_t        i;
    size_t        pos = 0;

    pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "{\"attributes\":{");
    for (i = 0; i < APPL_WRITE_BATCH_MAX; i++)
        pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "%s\"%u\":1",
                                i ? "," : "", (unsigned)i);
    pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "}}");
    CHECK_EQ(parse(js, &c), RES_OK);
    CHECK_EQ(c.n, APPL_WRITE_BATCH_MAX);

    pos = 0;
    pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "{\"attributes\":{");
    for (i = 0; i < APPL_WRITE_BATCH_MAX + 1u; i++)
        pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "%s\"%u\":1",
                                i ? "," : "", (unsigned)i);
    pos += (size_t)snprintf(js + pos, sizeof(js) - pos, "}}");
    CHECK_EQ(parse(js, &c), RES_ETOOBIG);
}

/*
 * An acknowledgement the sender cannot correlate is barely an
 * acknowledgement, so a tag too large to echo is refused rather than dropped.
 */
static void a_tag_too_large_to_echo_is_refused(void)
{
    control_cmd_t c;
    char          js[512];
    size_t        pos;
    size_t        i;

    pos = (size_t)snprintf(js, sizeof(js), "{\"tag\":\"");
    for (i = 0; i < CONTROL_TAG_MAX; i++)
        js[pos++] = 'x';
    pos += (size_t)snprintf(js + pos, sizeof(js) - pos,
                            "\",\"attributes\":{\"1\":1}}");
    js[pos] = '\0';

    CHECK_EQ(control_parse(js, pos, &c), RES_ETOOBIG);
}

static void malformed_documents_are_refused(void)
{
    static const char *bad[] = {
        "",
        "not json",
        "[1,2]",                          /* the root must be an object */
        "\"just a string\"",
        "{\"attributes\":{\"1\":1}",      /* unclosed                   */
        "{\"attributes\":{\"1\":}}"
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        control_cmd_t c;

        CHECK(parse(bad[i], &c) != RES_OK);
    }

    {
        control_cmd_t c;

        CHECK_EQ(control_parse(NULL, 4, &c), RES_EINVAL);
        CHECK_EQ(control_parse("{}", 2, NULL), RES_EINVAL);
    }
}

/* ------------------------------------------------- through to the appliance */

static void bring_up(appliance_t *a, link_t *l)
{
    appl_policy_t p;
    int           i;

    appl_policy_defaults(&p);
    p.sweep_interval_ms = 1000;
    appl_init(a, &p);
    for (i = 0; i < 20; i++)
        CHECK_EQ(appl_tick(a, l, 0), RES_OK);
    CHECK_EQ(appl_state(a), APPL_READY);
}

static void run_batch(appliance_t *a, link_t *l)
{
    int i;

    for (i = 0; i < 8 && !appl_write_done(a); i++)
        CHECK_EQ(appl_tick(a, l, 10), RES_OK);
    CHECK(appl_write_done(a));
}

static const char *ack(strbuf_t *sb, char *buf, size_t cap,
                       const appliance_t *a)
{
    write_code_map_t m;

    write_code_map_defaults(&m);
    sb_init(sb, buf, cap);
    CHECK_EQ(payload_ack(sb, "TS-1", "T", "7", 1, appl_write_batch(a), &m),
             RES_OK);
    return sb_str(sb);
}

/*
 * A command naming service 288 -- 0x0120, whose low byte is 0x20 -- reaches
 * 0x0120 on a multi-byte appliance and nothing else: parsed by control.c,
 * written through the model, sent as real packets by the simulator, and
 * acknowledged under the key it arrived with.
 */
static void a_command_for_a_two_byte_id_writes_that_service_only(void)
{
    sim_link_t    s;
    link_t        l;
    appliance_t   a;
    control_cmd_t c;
    char          buf[512];
    strbuf_t      sb;
    uint16_t      v = 0;
    const uint8_t u16[] = { 0x00, 0x00, 0x27, 0x10 };
    const appl_write_batch_t *b;

    sim_link_init(&s, 0xE0);
    CHECK_EQ(sim_link_set_identity(&s, TS_FORMAT_MULTIBYTE, TS_CLASS_SENSOR,
                                   0x00E0, "ACME", "PM-1"), RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x0120, 0x21, true, 1111, u16, 4),
             RES_OK);
    CHECK_EQ(sim_link_add_service(&s, 0x0020, 0x21, true, 2222, u16, 4),
             RES_OK);
    sim_link_bind(&s, &l);
    bring_up(&a, &l);

    /* The sweep read each under its own id. */
    CHECK_EQ(appl_value(&a, 0x0120, &v), RES_OK);
    CHECK_EQ(v, 1111);
    CHECK_EQ(appl_value(&a, 0x0020, &v), RES_OK);
    CHECK_EQ(v, 2222);
    CHECK_EQ(appl_value(&a, 0x0220, &v), RES_ENOTFOUND);

    CHECK_EQ(parse("{\"tag\":7,\"attributes\":{\"288\":4321}}", &c), RES_OK);
    CHECK_EQ(c.n, 1);
    CHECK_EQ(c.items[0].service_id, 0x0120);

    CHECK_EQ(appl_write_begin(&a, c.items, c.n), RES_OK);
    run_batch(&a, &l);

    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b)
        CHECK_EQ(b->items[0].result, APPL_WRITE_OK);

    CHECK_EQ(s.services[0].value, 4321);
    CHECK_EQ(s.services[1].value, 2222);
    CHECK_EQ(appl_value(&a, 0x0120, &v), RES_OK);
    CHECK_EQ(v, 4321);
    CHECK_EQ(appl_value(&a, 0x0020, &v), RES_OK);
    CHECK_EQ(v, 2222);

    CHECK(strstr(ack(&sb, buf, sizeof(buf), &a),
                 "\"results\":{\"288\":\"ok\"}") != NULL);
    appl_write_clear(&a);

    /* And the other way round: 32 writes 0x0020, not 0x0120. */
    CHECK_EQ(parse("{\"attributes\":{\"32\":99}}", &c), RES_OK);
    CHECK_EQ(appl_write_begin(&a, c.items, c.n), RES_OK);
    run_batch(&a, &l);
    CHECK_EQ(s.services[0].value, 4321);
    CHECK_EQ(s.services[1].value, 99);
}

/*
 * A general-format appliance has seven-bit ids. 288 is a well-formed key, so
 * the command is not refused as a whole -- but that appliance declared no such
 * service, so the item is `unknown` and costs nothing, and above all it is not
 * written to 0x20, the service its low byte names.
 */
static void a_two_byte_id_sent_to_a_general_appliance_is_unknown(void)
{
    sim_link_t    s;
    link_t        l;
    appliance_t   a;
    control_cmd_t c;
    char          buf[512];
    strbuf_t      sb;
    const uint8_t sup[] = { 0x00, 0x64 };
    const appl_write_batch_t *b;

    sim_link_init(&s, 0x04);
    CHECK_EQ(sim_link_add_service(&s, 0x20, 0, true, 5, sup, 2), RES_OK);
    sim_link_bind(&s, &l);
    bring_up(&a, &l);

    CHECK_EQ(parse("{\"attributes\":{\"288\":9,\"32\":6}}", &c), RES_OK);
    CHECK_EQ(c.n, 2);
    CHECK_EQ(appl_write_begin(&a, c.items, c.n), RES_OK);
    run_batch(&a, &l);

    b = appl_write_batch(&a);
    CHECK(b != NULL);
    if (b) {
        CHECK_EQ(b->items[0].service_id, 0x0120);
        CHECK_EQ(b->items[0].result, APPL_WRITE_UNKNOWN);
        CHECK_EQ(b->items[1].service_id, 0x0020);
        CHECK_EQ(b->items[1].result, APPL_WRITE_OK);
    }
    CHECK_EQ(s.services[0].value, 6);

    CHECK(strstr(ack(&sb, buf, sizeof(buf), &a),
                 "\"results\":{\"288\":\"unknown\",\"32\":\"ok\"}") != NULL);
}

void test_control(void)
{
    a_command_becomes_a_batch();
    a_tag_of_any_type_is_copied_raw();
    a_command_without_a_tag_is_still_a_command();
    a_command_that_asks_for_nothing_is_refused();
    a_key_that_is_not_a_service_id_is_refused();
    a_value_that_will_not_fit_is_refused();
    more_than_a_batch_holds_is_refused();
    a_tag_too_large_to_echo_is_refused();
    malformed_documents_are_refused();
    a_command_for_a_two_byte_id_writes_that_service_only();
    a_two_byte_id_sent_to_a_general_appliance_is_unknown();
}
