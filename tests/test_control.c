/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is where a stranger's bytes become instructions for an appliance, so
 * most of what is worth testing here is what it refuses.
 */
#include "tests.h"

#include "core/mqtt/control.h"

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
        "{\"attributes\":{\"128\":1}}",     /* past the seven bits */
        "{\"attributes\":{\"1000\":1}}"
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        control_cmd_t c;

        CHECK_EQ(parse(bad[i], &c), RES_EFORMAT);
    }

    /* The boundary itself is fine. */
    {
        control_cmd_t c;

        CHECK_EQ(parse("{\"attributes\":{\"127\":1}}", &c), RES_OK);
        CHECK_EQ(c.items[0].service_id, 127);
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

    pos += (size_t)sprintf(js + pos, "{\"attributes\":{");
    for (i = 0; i < APPL_WRITE_BATCH_MAX; i++)
        pos += (size_t)sprintf(js + pos, "%s\"%u\":1",
                               i ? "," : "", (unsigned)i);
    pos += (size_t)sprintf(js + pos, "}}");
    CHECK_EQ(parse(js, &c), RES_OK);
    CHECK_EQ(c.n, APPL_WRITE_BATCH_MAX);

    pos = 0;
    pos += (size_t)sprintf(js + pos, "{\"attributes\":{");
    for (i = 0; i < APPL_WRITE_BATCH_MAX + 1u; i++)
        pos += (size_t)sprintf(js + pos, "%s\"%u\":1",
                               i ? "," : "", (unsigned)i);
    pos += (size_t)sprintf(js + pos, "}}");
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

    pos = (size_t)sprintf(js, "{\"tag\":\"");
    for (i = 0; i < CONTROL_TAG_MAX; i++)
        js[pos++] = 'x';
    pos += (size_t)sprintf(js + pos, "\",\"attributes\":{\"1\":1}}");
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
}
