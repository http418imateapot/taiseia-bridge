/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "core/util/jsonr.h"
#include "core/util/jsonw.h"
#include "core/util/strbuf.h"

/* ---------------------------------------------------------------- strbuf */

static void a_string_builder_appends_and_terminates(void)
{
    char     buf[32];
    strbuf_t sb;

    sb_init(&sb, buf, sizeof(buf));
    CHECK(sb_ok(&sb));
    CHECK_EQ(sb_len(&sb), 0);
    CHECK_STREQ(sb_str(&sb), "");

    sb_puts(&sb, "ab");
    sb_putc(&sb, 'c');
    sb_putu(&sb, 0);
    sb_putu(&sb, 4090);
    sb_puti(&sb, -7);

    CHECK(sb_ok(&sb));
    CHECK_STREQ(sb_str(&sb), "abc04090-7");
    CHECK_EQ(sb_len(&sb), 10);

    sb_reset(&sb);
    CHECK_EQ(sb_len(&sb), 0);
    CHECK_STREQ(sb_str(&sb), "");
}

/*
 * Overflow is sticky on purpose. A buffer holding the first n bytes of a JSON
 * document looks structurally plausible and is silently truncated, so once
 * anything is dropped nothing else is written and one check at the end
 * catches it.
 */
static void overflow_is_sticky(void)
{
    char     buf[8];
    strbuf_t sb;

    sb_init(&sb, buf, sizeof(buf));
    sb_puts(&sb, "1234567");        /* exactly fills it, terminator included */
    CHECK(sb_ok(&sb));
    CHECK_EQ(sb_len(&sb), 7);

    sb_putc(&sb, 'x');
    CHECK(!sb_ok(&sb));
    CHECK_STREQ(sb_str(&sb), "1234567");

    /* Something that would have fit is still dropped. */
    sb_reset(&sb);
    sb_puts(&sb, "12345678");
    CHECK(!sb_ok(&sb));
    sb_putc(&sb, 'a');
    CHECK(!sb_ok(&sb));
    CHECK_EQ(sb_len(&sb), 0);

    /* A zero-capacity buffer is usable and useless, not a crash. */
    sb_init(&sb, buf, 0);
    CHECK(!sb_ok(&sb));
    sb_putc(&sb, 'a');
    CHECK_STREQ(sb_str(&sb), "");

    sb_init(&sb, NULL, 10);
    CHECK(!sb_ok(&sb));
    CHECK_STREQ(sb_str(&sb), "");
}

static void extreme_integers_format_correctly(void)
{
    char     buf[64];
    strbuf_t sb;

    sb_init(&sb, buf, sizeof(buf));
    sb_putu(&sb, 18446744073709551615u);
    CHECK_STREQ(sb_str(&sb), "18446744073709551615");

    /* Negating the most negative value is undefined; it must still print. */
    sb_reset(&sb);
    sb_puti(&sb, (-9223372036854775807LL - 1));
    CHECK_STREQ(sb_str(&sb), "-9223372036854775808");

    sb_reset(&sb);
    {
        const uint8_t raw[] = { 0x00, 0x0f, 0xa5, 0xff };

        sb_puthex(&sb, raw, sizeof(raw));
        CHECK_STREQ(sb_str(&sb), "000fa5ff");
    }
}

/* ----------------------------------------------------------------- jsonw */

static void an_object_gets_its_commas_right(void)
{
    char     buf[256];
    strbuf_t sb;
    jsonw_t  w;

    sb_init(&sb, buf, sizeof(buf));
    jw_init(&w, &sb);

    jw_obj_open(&w);
    jw_kstr(&w, "id", "TS-1");
    jw_kuint(&w, "n", 42);
    jw_kint(&w, "t", -3);
    jw_kbool(&w, "up", true);
    jw_knull(&w, "z");
    jw_key(&w, "list");
    jw_arr_open(&w);
    jw_uint(&w, 1);
    jw_uint(&w, 2);
    jw_arr_close(&w);
    jw_key(&w, "inner");
    jw_obj_open(&w);
    jw_kuint(&w, "a", 1);
    jw_obj_close(&w);
    jw_obj_close(&w);

    CHECK(jw_ok(&w));
    CHECK_STREQ(sb_str(&sb),
                "{\"id\":\"TS-1\",\"n\":42,\"t\":-3,\"up\":true,\"z\":null,"
                "\"list\":[1,2],\"inner\":{\"a\":1}}");
}

static void numeric_keys_are_quoted(void)
{
    char     buf[64];
    strbuf_t sb;
    jsonw_t  w;

    sb_init(&sb, buf, sizeof(buf));
    jw_init(&w, &sb);

    jw_obj_open(&w);
    jw_key_uint(&w, 0);
    jw_uint(&w, 1);
    jw_key_uint(&w, 7);
    jw_uint(&w, 62);
    jw_obj_close(&w);

    CHECK(jw_ok(&w));
    CHECK_STREQ(sb_str(&sb), "{\"0\":1,\"7\":62}");
}

static void strings_are_escaped(void)
{
    char     buf[128];
    strbuf_t sb;
    jsonw_t  w;

    sb_init(&sb, buf, sizeof(buf));
    jw_init(&w, &sb);
    jw_str(&w, "a\"b\\c\nd\te\x01");
    CHECK(jw_ok(&w));
    CHECK_STREQ(sb_str(&sb), "\"a\\\"b\\\\c\\nd\\te\\u0001\"");

    /* Bytes at or above 0x80 are already UTF-8 and pass through untouched. */
    sb_reset(&sb);
    jw_init(&w, &sb);
    jw_str(&w, "\xE6\xB8\xAC");
    CHECK(jw_ok(&w));
    CHECK_STREQ(sb_str(&sb), "\"\xE6\xB8\xAC\"");
}

/*
 * A value whose JSON type is not ours to choose goes back exactly as it
 * arrived. Decoding and re-encoding it could change 1.0 into 1, or reorder an
 * object, and then it no longer compares equal to what the sender sent.
 */
static void raw_values_are_spliced_verbatim(void)
{
    char     buf[128];
    strbuf_t sb;
    jsonw_t  w;

    sb_init(&sb, buf, sizeof(buf));
    jw_init(&w, &sb);

    jw_obj_open(&w);
    jw_kraw(&w, "tag", "{\"k\":[1,2]}", 11);
    jw_kraw(&w, "empty", NULL, 0);
    jw_obj_close(&w);

    CHECK(jw_ok(&w));
    CHECK_STREQ(sb_str(&sb), "{\"tag\":{\"k\":[1,2]},\"empty\":null}");
}

static void a_writer_reports_overflow_and_bad_nesting(void)
{
    char     buf[8];
    strbuf_t sb;
    jsonw_t  w;
    int      i;

    sb_init(&sb, buf, sizeof(buf));
    jw_init(&w, &sb);
    jw_obj_open(&w);
    jw_kstr(&w, "key", "a rather long value");
    jw_obj_close(&w);
    CHECK(!jw_ok(&w));

    /* Closing more than was opened is caught, not silently accepted. */
    {
        char     big[64];
        strbuf_t sb2;

        sb_init(&sb2, big, sizeof(big));
        jw_init(&w, &sb2);
        jw_obj_open(&w);
        jw_obj_close(&w);
        jw_obj_close(&w);
        CHECK(!jw_ok(&w));
    }

    /* And so is nesting past the depth limit. */
    {
        char     big[256];
        strbuf_t sb3;

        sb_init(&sb3, big, sizeof(big));
        jw_init(&w, &sb3);
        for (i = 0; i <= JW_MAX_DEPTH; i++)
            jw_arr_open(&w);
        CHECK(!jw_ok(&w));
    }
}

/* ----------------------------------------------------------------- jsonr */

static void an_object_parses_into_tokens(void)
{
    static const char js[] =
        "{\"tag\":1,\"attributes\":{\"3\":70,\"0\":1},\"ok\":true}";
    jr_tok_t toks[JR_MAX_TOKENS];
    int      n = 0;
    int      v;
    int64_t  i64 = 0;
    uint16_t u16 = 0;
    bool     b = false;

    CHECK_EQ(jr_parse(js, sizeof(js) - 1, toks, JR_MAX_TOKENS, &n), RES_OK);
    CHECK(n > 0);
    CHECK_EQ(toks[0].type, JR_OBJECT);
    CHECK_EQ(toks[0].size, 3);

    v = jr_obj_get(js, toks, n, 0, "tag");
    CHECK(v > 0);
    CHECK_EQ(jr_int(js, &toks[v], &i64), RES_OK);
    CHECK_EQ(i64, 1);

    v = jr_obj_get(js, toks, n, 0, "ok");
    CHECK(v > 0);
    CHECK_EQ(jr_bool(js, &toks[v], &b), RES_OK);
    CHECK_EQ(b, 1);

    v = jr_obj_get(js, toks, n, 0, "attributes");
    CHECK(v > 0);
    CHECK_EQ(toks[v].type, JR_OBJECT);
    CHECK_EQ(toks[v].size, 2);

    {
        int cursor = 0;
        int k;
        int seen = 0;

        while ((k = jr_obj_iter(toks, n, v, &cursor)) >= 0) {
            seen++;
            if (jr_streq(js, &toks[k], "3")) {
                CHECK_EQ(jr_uint16(js, &toks[k + 1], &u16), RES_OK);
                CHECK_EQ(u16, 70);
            }
        }
        CHECK_EQ(seen, 2);
    }

    CHECK_EQ(jr_obj_get(js, toks, n, 0, "absent"), -1);
}

static void a_tag_of_any_type_survives_a_round_trip(void)
{
    static const char *cases[] = {
        "{\"tag\":1}",
        "{\"tag\":\"abc\"}",
        "{\"tag\":{\"k\":[1,2]}}",
        "{\"tag\":[1,\"two\",null]}",
        "{\"tag\":null}",
        "{\"tag\":-12}"
    };
    static const char *expect[] = {
        "1", "\"abc\"", "{\"k\":[1,2]}", "[1,\"two\",null]", "null", "-12"
    };
    size_t c;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        jr_tok_t    toks[JR_MAX_TOKENS];
        int         n = 0;
        int         v;
        const char *p = NULL;
        size_t      len = 0;
        char        out[64];

        CHECK_EQ(jr_parse(cases[c], strlen(cases[c]), toks, JR_MAX_TOKENS, &n),
                 RES_OK);
        v = jr_obj_get(cases[c], toks, n, 0, "tag");
        CHECK(v > 0);
        if (v <= 0)
            continue;

        jr_raw(cases[c], &toks[v], &p, &len);
        CHECK(len < sizeof(out));
        memcpy(out, p, len);
        out[len] = '\0';
        CHECK_STREQ(out, expect[c]);
    }
}

static void escapes_are_decoded(void)
{
    static const char js[] =
        "{\"s\":\"a\\\"b\\\\c\\nd\\u0041\\u6E2C\\uD83D\\uDE00\"}";
    jr_tok_t toks[JR_MAX_TOKENS];
    int      n = 0;
    int      v;
    char     out[64];

    CHECK_EQ(jr_parse(js, sizeof(js) - 1, toks, JR_MAX_TOKENS, &n), RES_OK);
    v = jr_obj_get(js, toks, n, 0, "s");
    CHECK(v > 0);
    CHECK_EQ(jr_str(js, &toks[v], out, sizeof(out)), RES_OK);
    /* The last two escapes are a surrogate pair for U+1F600. */
    CHECK_STREQ(out, "a\"b\\c\ndA\xE6\xB8\xAC\xF0\x9F\x98\x80");

    CHECK_EQ(jr_str(js, &toks[v], out, 4), RES_ETOOBIG);
}

static void malformed_documents_are_refused(void)
{
    static const char *bad[] = {
        "",
        "{",
        "}",
        "{\"a\":}",
        "{\"a\" 1}",
        "[1,2",
        "{\"a\":1]",
        "\"unterminated",
        "{\"a\":\"bad\\qescape\"}",
        "{\"a\":\"\x01\"}",
        "{\"a\":tru}",
        "{\"a\":01}",
        "{\"a\":1.}",
        "{\"a\":1e}",
        "{\"a\":+1}",
        "{\"a\":1,}",
        "[1,]",
        "{\"a\":1}{\"b\":2}",
        "1 2",
        "@"
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        jr_tok_t toks[JR_MAX_TOKENS];
        int      n = 0;

        CHECK(jr_parse(bad[i], strlen(bad[i]), toks, JR_MAX_TOKENS, &n)
              != RES_OK);
    }

    /* A lone surrogate half decodes to nothing valid. */
    {
        static const char js[] = "{\"s\":\"\\uD83D\"}";
        jr_tok_t toks[JR_MAX_TOKENS];
        int      n = 0;
        int      v;
        char     out[16];

        CHECK_EQ(jr_parse(js, sizeof(js) - 1, toks, JR_MAX_TOKENS, &n),
                 RES_OK);
        v = jr_obj_get(js, toks, n, 0, "s");
        CHECK(v > 0);
        CHECK_EQ(jr_str(js, &toks[v], out, sizeof(out)), RES_EFORMAT);
    }
}

/* Hostile input must not exhaust the token array or the stack. */
static void depth_and_token_limits_hold(void)
{
    char     deep[64];
    jr_tok_t toks[JR_MAX_TOKENS];
    jr_tok_t few[3];
    int      n = 0;
    size_t   i;

    for (i = 0; i < JR_MAX_DEPTH + 2u; i++)
        deep[i] = '[';
    CHECK(jr_parse(deep, JR_MAX_DEPTH + 2u, toks, JR_MAX_TOKENS, &n)
          != RES_OK);

    CHECK_EQ(jr_parse("{\"a\":1,\"b\":2,\"c\":3}", 19, few, 3, &n),
             RES_ETOOBIG);

    CHECK_EQ(jr_parse(NULL, 4, toks, JR_MAX_TOKENS, &n), RES_EINVAL);
    CHECK_EQ(jr_parse("{}", 2, NULL, JR_MAX_TOKENS, &n), RES_EINVAL);
    CHECK_EQ(jr_parse("{}", 2, toks, 0, &n), RES_EINVAL);
    CHECK_EQ(jr_parse("{}", 2, toks, JR_MAX_TOKENS, NULL), RES_EINVAL);
}

static void accessors_reject_the_wrong_type(void)
{
    static const char js[] = "{\"s\":\"x\",\"n\":7,\"big\":99999}";
    jr_tok_t toks[JR_MAX_TOKENS];
    int      n = 0;
    int64_t  i64 = 0;
    uint16_t u16 = 0;
    bool     b = false;
    char     out[8];
    int      s, num, big;

    CHECK_EQ(jr_parse(js, sizeof(js) - 1, toks, JR_MAX_TOKENS, &n), RES_OK);
    s   = jr_obj_get(js, toks, n, 0, "s");
    num = jr_obj_get(js, toks, n, 0, "n");
    big = jr_obj_get(js, toks, n, 0, "big");
    CHECK(s > 0 && num > 0 && big > 0);

    CHECK_EQ(jr_int(js, &toks[s], &i64), RES_EFORMAT);
    CHECK_EQ(jr_bool(js, &toks[num], &b), RES_EFORMAT);
    CHECK_EQ(jr_str(js, &toks[num], out, sizeof(out)), RES_EFORMAT);
    CHECK_EQ(jr_uint16(js, &toks[big], &u16), RES_ETOOBIG);
    CHECK_EQ(jr_int(js, &toks[num], NULL), RES_EINVAL);

    CHECK(!jr_streq(js, &toks[num], "7"));   /* not a string token */
    CHECK(jr_streq(js, &toks[s], "x"));
}

void test_json(void)
{
    a_string_builder_appends_and_terminates();
    overflow_is_sticky();
    extreme_integers_format_correctly();
    an_object_gets_its_commas_right();
    numeric_keys_are_quoted();
    strings_are_escaped();
    raw_values_are_spliced_verbatim();
    a_writer_reports_overflow_and_bad_nesting();
    an_object_parses_into_tokens();
    a_tag_of_any_type_survives_a_round_trip();
    escapes_are_decoded();
    malformed_documents_are_refused();
    depth_and_token_limits_hold();
    accessors_reject_the_wrong_type();
}
