/* SPDX-License-Identifier: Apache-2.0
 * Compiled in CI, outside the product CMake build. Emits measured JSON.
 * Synthetic SA/service keys below are NOT appliance service definitions.
 */
#include "metadata.h"
#include "core/taiseia/datatype.h"
#include "core/taiseia/frame.h"
#include "reference.h"
#include <stdio.h>
#include <string.h>

#define BYTE_PAIR_COUNT 65536u
#define SYNTHETIC_SA 0x7Eu
#define SYNTHETIC_SERVICE 0x01u

static unsigned checks = 0;
static unsigned failures = 0;
#define CHECK(p) do { ++checks; if (!(p)) { ++failures; \
    fprintf(stderr, "line %d: %s\n", __LINE__, #p); } } while (0)

static uint8_t code_named(const char *name)
{
    size_t i;
    for (i = 0; i < ts_datatype_count(); ++i) {
        const ts_datatype_t *t = ts_datatype_at(i);
        if (strcmp(t->name, name) == 0) return t->code;
    }
    CHECK(0);
    return 0;
}

static void matrix(void)
{
    size_t i;
    int first = 1;
    const char *readings[] = { "wide", "low", "strict", "lenient" };
    printf("\"cells\":[");
    for (i = 0; i < ts_datatype_count(); ++i) {
        const ts_datatype_t *t = ts_datatype_at(i);
        int32_t lo = 0;
        int32_t hi = 0;
        int layout;
        if (ts_general_domain(t->code, &lo, &hi) != RES_OK) continue;
        for (layout = 0; layout < 2; ++layout) {
            int reading;
            for (reading = 0; reading < 4; ++reading) {
                int32_t v;
                int32_t witness = 0;
                unsigned bad = 0;
                for (v = lo; v <= hi; ++v) {
                    uint8_t bytes[TS_GENERAL_DATA_BYTES];
                    uint8_t want[TS_GENERAL_DATA_BYTES];
                    int32_t back = 0;
                    result_t rc = ts_general_encode(t->code, (ts_layout_t)layout,
                                                    v, bytes, sizeof bytes);
                    CHECK(ref_encode(t->code, layout, v, want) == 1);
                    CHECK(rc == RES_OK);
                    if (rc != RES_OK) continue;
                    CHECK(memcmp(bytes, want, sizeof bytes) == 0);
                    rc = ts_general_decode(t->code, (ts_reading_t)reading,
                                            bytes, sizeof bytes, &back);
                    if (rc != RES_OK || back != v) {
                        if (bad == 0) witness = v;
                        ++bad;
                    }
                }
                printf("%s{\"type\":\"%s\",\"layout\":\"%c\",\"reading\":\"%s\","
                       "\"misread\":%u,\"domain\":%ld,\"witness\":",
                       first ? "" : ",", t->name, 'A' + layout, readings[reading],
                       bad, (long)(hi - lo + 1));
                if (bad) printf("%ld", (long)witness); else printf("null");
                printf("}");
                first = 0;
            }
        }
    }
    printf("],\n");
}

static void exactness(void)
{
    static unsigned char image[BYTE_PAIR_COUNT];
    size_t i;
    int first = 1;
    const char *readings[] = { "wide", "low", "strict", "lenient" };
    printf("\"exactness\":[");
    for (i = 0; i < ts_datatype_count(); ++i) {
        const ts_datatype_t *t = ts_datatype_at(i);
        int32_t lo = 0;
        int32_t hi = 0;
        int layout;
        int reading;
        if (ts_general_domain(t->code, &lo, &hi) != RES_OK) continue;
        memset(image, 0, sizeof image);
        for (layout = 0; layout < 2; ++layout) {
            int32_t v;
            for (v = lo; v <= hi; ++v) {
                uint8_t b[TS_GENERAL_DATA_BYTES];
                int ok = ref_encode(t->code, layout, v, b);
                CHECK(ok == 1);
                if (ok) image[(unsigned)b[0] * 256u + b[1]] = 1;
            }
        }
        for (reading = 0; reading < 4; ++reading) {
            unsigned pair;
            unsigned over = 0;
            unsigned under = 0;
            for (pair = 0; pair < BYTE_PAIR_COUNT; ++pair) {
                uint8_t b[TS_GENERAL_DATA_BYTES];
                int32_t got = 0;
                int32_t want = 0;
                int accepted;
                int expected;
                b[0] = (uint8_t)(pair / 256u);
                b[1] = (uint8_t)(pair % 256u);
                accepted = ts_general_decode(t->code, (ts_reading_t)reading,
                                              b, sizeof b, &got) == RES_OK;
                expected = ref_decode(t->code, reading, b, &want);
                CHECK(accepted == expected);
                if (accepted) CHECK(got == want);
                if (accepted && !image[pair]) ++over;
                if (!accepted && image[pair]) ++under;
            }
            printf("%s{\"type\":\"%s\",\"reading\":\"%s\","
                   "\"over_accepted\":%u,\"under_accepted\":%u}",
                   first ? "" : ",", t->name, readings[reading], over, under);
            first = 0;
        }
    }
    printf("],\n");
}

static void rejected_rows_prevent_startup(void)
{
    const char *bad[] = { "7E:01:FF", "7E:01:22", "7E:81:30", "7E:01:3Z",
                          "", "7E:01:30extra", "7E-01-30" };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        lab_meta_config_t c;
        uint8_t dtype = 0;
        CHECK(lab_meta_init(&c) == RES_OK);
        CHECK(lab_meta_add(&c, "7E:01:30", LAB_META_LINE_BYTES) == RES_OK);
        CHECK(lab_meta_add(&c, bad[i], strlen(bad[i])) != RES_OK);
        CHECK(lab_meta_finish(&c) != RES_OK);
        CHECK(lab_meta_config_lookup(&c, SYNTHETIC_SA, SYNTHETIC_SERVICE,
                                     &dtype) != RES_OK);
    }
    {
        lab_meta_config_t c;
        CHECK(lab_meta_init(&c) == RES_OK);
        CHECK(lab_meta_add(&c, "7E:01:30", LAB_META_LINE_BYTES) == RES_OK);
        CHECK(lab_meta_add(&c, "7E:01:20", LAB_META_LINE_BYTES) == RES_EFORMAT);
        CHECK(lab_meta_finish(&c) == RES_EFORMAT);
        CHECK(lab_meta_init(&c) == RES_OK);
        for (i = 0; i < LAB_META_CAPACITY; ++i) {
            char line[LAB_META_LINE_BYTES + 1u];
            int n = snprintf(line, sizeof line, "7E:%02X:30", (unsigned)i);
            CHECK(n == (int)LAB_META_LINE_BYTES);
            CHECK(lab_meta_add(&c, line, LAB_META_LINE_BYTES) == RES_OK);
        }
        CHECK(lab_meta_add(&c, "7D:00:30", LAB_META_LINE_BYTES) == RES_ETOOBIG);
        CHECK(lab_meta_finish(&c) == RES_EFORMAT);
    }
}

static void three_adapters_and_wire_responses(void)
{
    lab_meta_config_t config;
    lab_meta_entry_t entry;
    lab_meta_registration_t registration;
    lab_meta_port_t ports[3];
    const char *names[] = { "config", "registration", "null" };
    const uint8_t highs[] = { 0x00, 0xFF, 0x12 };
    unsigned source;
    int first = 1;
    CHECK(lab_meta_init(&config) == RES_OK);
    CHECK(lab_meta_add(&config, "7E:01:30", LAB_META_LINE_BYTES) == RES_OK);
    CHECK(lab_meta_finish(&config) == RES_OK);
    entry.sa_id = SYNTHETIC_SA;
    entry.service_id = SYNTHETIC_SERVICE;
    entry.dtype = code_named("INT8");
    registration.entries = &entry;
    registration.count = 1;
    registration.capacity = 1;
    registration.has_wire_types = true;
    ports[0].lookup = lab_meta_config_lookup;
    ports[0].context = &config;
    ports[1].lookup = lab_meta_registration_lookup;
    ports[1].context = &registration;
    ports[2].lookup = NULL;
    ports[2].context = NULL;
    printf("\"responses\":[");
    for (source = 0; source < 3u; ++source) {
        unsigned variant;
        for (variant = 0; variant < sizeof highs; ++variant) {
            uint8_t wire[TS_GENERAL_REQ_LEN];
            ts_frame_t frame;
            int32_t value = 0;
            int32_t raw;
            result_t rc;
            /* Independent test peer builds bytes, not a decoder round trip. */
            wire[0] = (uint8_t)sizeof wire;
            wire[1] = SYNTHETIC_SA;
            wire[2] = SYNTHETIC_SERVICE;
            wire[3] = highs[variant];
            wire[4] = 0xFD;
            wire[5] = wire[0] ^ wire[1] ^ wire[2] ^ wire[3] ^ wire[4];
            CHECK(ts_parse_response(wire, sizeof wire, &frame) == RES_OK);
            CHECK(frame.data_len == TS_GENERAL_DATA_BYTES);
            rc = lab_typed_read(&ports[source], frame.sa_id, frame.service_id,
                                TS_READ_LENIENT, frame.data, frame.data_len, &value);
            raw = (int32_t)((uint32_t)highs[variant] * 256u + 0xFDu);
            if (source == 2u) {
                CHECK(rc == RES_ENOTFOUND);
                CHECK(value == raw);
            } else if (variant == 2u) {
                CHECK(rc == RES_EFORMAT);
                CHECK(value == raw);
            } else {
                CHECK(rc == RES_OK);
                CHECK(value == -3);
            }
            printf("%s{\"source\":\"%s\",\"bytes\":\"%02X FD\","
                   "\"result\":%d,\"value\":%ld,\"raw\":%ld}",
                   first ? "" : ",", names[source], (unsigned)highs[variant],
                   (int)rc, (long)value, (long)raw);
            first = 0;
            wire[5] ^= 1u;
            CHECK(ts_parse_response(wire, sizeof wire, &frame) == RES_EFORMAT);
        }
    }
    printf("],\n");
    {
        uint8_t dtype = 0;
        uint8_t b[TS_GENERAL_DATA_BYTES] = { 0x5A, 0xA5 };
        registration.has_wire_types = false;
        CHECK(lab_meta_lookup(&ports[1], SYNTHETIC_SA, SYNTHETIC_SERVICE,
                              &dtype) == RES_ENOTFOUND);
        registration.has_wire_types = true;
        entry.dtype = code_named("UINT32");
        CHECK(lab_meta_lookup(&ports[1], SYNTHETIC_SA, SYNTHETIC_SERVICE,
                              &dtype) == RES_EUNSUPPORTED);
        CHECK(lab_typed_encode(&ports[2], SYNTHETIC_SA, SYNTHETIC_SERVICE,
                               TS_LAYOUT_SIGN_EXTEND, -3, b, sizeof b) == RES_ENOTFOUND);
        CHECK(b[0] == 0x5A && b[1] == 0xA5);
        CHECK(lab_typed_encode(&ports[0], SYNTHETIC_SA, SYNTHETIC_SERVICE,
                               TS_LAYOUT_SIGN_EXTEND, -3, b, sizeof b) == RES_OK);
        CHECK(b[0] == 0xFF && b[1] == 0xFD);
        CHECK(lab_typed_encode(&ports[0], SYNTHETIC_SA, SYNTHETIC_SERVICE,
                               TS_LAYOUT_ZERO_FILL, -3, b, sizeof b) == RES_OK);
        CHECK(b[0] == 0x00 && b[1] == 0xFD);
    }
}

int main(void)
{
    printf("{\"subject\":\"production codec plus lab-only metadata prototype\",\n");
    matrix();
    exactness();
    rejected_rows_prevent_startup();
    three_adapters_and_wire_responses();
    printf("\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 1 : 0;
}
