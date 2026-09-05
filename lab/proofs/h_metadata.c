/* SPDX-License-Identifier: Apache-2.0
 * Eleven entry points for the R21 LAB subject, not production metadata.
 * Bounds are object capacities. Callback code is assumed memory safe; its
 * result code and output byte are unrestricted. No actual Annex A facts.
 */
#include "metadata.h"
#include "reference.h"

uint8_t nondet_u8(void);
int nondet_int(void);
result_t nondet_result(void);
int32_t nondet_i32(void);
size_t nondet_size(void);
bool nondet_bool(void);

void h_parse(void);
void h_bad_line(void);
void h_duplicate(void);
void h_capacity(void);
void h_config(void);
void h_registration(void);
void h_port(void);
void h_no_metadata(void);
void h_read(void);
void h_write(void);
void h_arguments(void);

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

void h_parse(void)
{
    char line[LAB_META_LINE_BYTES + 1u];
    size_t len = nondet_size();
    lab_meta_entry_t out = { 0xAA, 0xBB, 0xCC };
    result_t rc;
    unsigned width = 0;
    int sign = 0;
    __CPROVER_assume(len <= sizeof line);
    /* Uninitialized local bytes are arbitrary inputs in CBMC. */
    rc = lab_meta_parse(line, len, &out);
    __CPROVER_assert(rc == RES_OK || rc == RES_EFORMAT || rc == RES_EUNSUPPORTED,
                     "parser has a bounded result vocabulary");
    if (rc == RES_OK) {
        __CPROVER_assert(len == LAB_META_LINE_BYTES && line[2] == ':' && line[5] == ':',
                         "parser accepts only the declared grammar");
        __CPROVER_assert(is_hex(line[0]) && is_hex(line[1]) && is_hex(line[3]) &&
                         is_hex(line[4]) && is_hex(line[6]) && is_hex(line[7]),
                         "each field contains two hex digits");
        __CPROVER_assert(out.service_id < 0x80u, "service write bit is forbidden");
        __CPROVER_assert(ref_carryable(out.dtype, &width, &sign),
                         "parser accepts only carryable types");
    } else {
        __CPROVER_assert(out.sa_id == 0xAA && out.service_id == 0xBB && out.dtype == 0xCC,
                         "failed parse leaves destination untouched");
    }
}

void h_bad_line(void)
{
    char line[LAB_META_LINE_BYTES + 1u];
    size_t len = nondet_size();
    lab_meta_config_t config;
    result_t rc;
    __CPROVER_assume(len <= sizeof line);
    __CPROVER_assert(lab_meta_init(&config) == RES_OK, "init succeeds");
    rc = lab_meta_add(&config, line, len);
    if (rc != RES_OK) {
        uint8_t out = 0;
        __CPROVER_assert(lab_meta_finish(&config) != RES_OK, "bad row prevents startup");
        __CPROVER_assert(lab_meta_config_lookup(&config, 0, 0, &out) != RES_OK,
                         "bad row never exposes a partial configuration");
        __CPROVER_assert(lab_meta_add(&config, "7E:01:30", LAB_META_LINE_BYTES) != RES_OK,
                         "a later good row cannot clear failure");
    } else {
        __CPROVER_assert(lab_meta_finish(&config) == RES_OK, "valid row permits startup");
    }
}

void h_duplicate(void)
{
    lab_meta_config_t c;
    __CPROVER_assert(lab_meta_init(&c) == RES_OK, "init succeeds");
    __CPROVER_assert(lab_meta_add(&c, "7E:01:30", LAB_META_LINE_BYTES) == RES_OK,
                     "first declaration accepted");
    __CPROVER_assert(lab_meta_add(&c, "7E:01:20", LAB_META_LINE_BYTES) == RES_EFORMAT,
                     "duplicate key rejected even with different type");
    __CPROVER_assert(lab_meta_finish(&c) == RES_EFORMAT, "duplicate prevents startup");
}

void h_capacity(void)
{
    lab_meta_config_t c;
    size_t i;
    __CPROVER_assert(lab_meta_init(&c) == RES_OK, "init succeeds");
    for (i = 0; i < LAB_META_CAPACITY; ++i) {
        char line[LAB_META_LINE_BYTES] = { '7', 'E', ':', '0', '0', ':', '3', '0' };
        line[4] = (char)('0' + i);
        __CPROVER_assert(lab_meta_add(&c, line, sizeof line) == RES_OK,
                         "all capacity slots can be used");
    }
    __CPROVER_assert(lab_meta_add(&c, "7D:00:30", LAB_META_LINE_BYTES) == RES_ETOOBIG,
                     "capacity overflow refused without writing out of bounds");
    __CPROVER_assert(lab_meta_finish(&c) == RES_EFORMAT, "full table error prevents startup");
}

void h_config(void)
{
    lab_meta_config_t c;
    uint8_t out = 0xAA;
    uint8_t sa = nondet_u8();
    uint8_t sid = nondet_u8();
    result_t rc;
    c.count = nondet_size();
    c.ready = nondet_bool();
    rc = lab_meta_config_lookup(&c, sa, sid, &out);
    if (rc == RES_OK) {
        size_t i;
        bool found = false;
        __CPROVER_assert(c.ready && c.count <= LAB_META_CAPACITY,
                         "only a completed bounded config is queried");
        for (i = 0; i < c.count; ++i) {
            if (c.entries[i].sa_id == sa && c.entries[i].service_id == sid &&
                c.entries[i].dtype == out) found = true;
        }
        __CPROVER_assert(found, "config returns a matching row");
    } else {
        __CPROVER_assert(out == 0xAA, "lookup failure preserves output");
    }
}

void h_registration(void)
{
    lab_meta_entry_t entries[LAB_META_CAPACITY];
    lab_meta_registration_t registration;
    lab_meta_port_t port;
    uint8_t out = 0xAA;
    unsigned width = 0;
    int sign = 0;
    result_t rc;
    registration.entries = entries;
    registration.capacity = LAB_META_CAPACITY;
    registration.count = nondet_size();
    registration.has_wire_types = nondet_bool();
    port.lookup = lab_meta_registration_lookup;
    port.context = &registration;
    rc = lab_meta_lookup(&port, nondet_u8(), nondet_u8(), &out);
    if (rc == RES_OK) {
        __CPROVER_assert(registration.has_wire_types,
                         "untyped general registration cannot manufacture a type");
        __CPROVER_assert(ref_carryable(out, &width, &sign),
                         "wire types still pass carryability check");
    } else __CPROVER_assert(out == 0xAA, "registration failure preserves output");
}

static result_t arbitrary_adapter(const void *context, uint8_t sa, uint8_t sid,
                                  uint8_t *out)
{
    (void)context;
    (void)sa;
    (void)sid;
    if (nondet_bool()) *out = nondet_u8();
    return nondet_result();
}

void h_port(void)
{
    lab_meta_port_t port = { arbitrary_adapter, NULL };
    uint8_t out = 0xAA;
    unsigned width = 0;
    int sign = 0;
    result_t rc = lab_meta_lookup(&port, nondet_u8(), nondet_u8(), &out);
    __CPROVER_assert(rc == RES_OK || rc == RES_EINVAL || rc == RES_EFORMAT ||
                     rc == RES_ETOOBIG || rc == RES_EIO || rc == RES_ENOTFOUND ||
                     rc == RES_EUNSUPPORTED, "port bounds arbitrary adapter result codes");
    if (rc == RES_OK)
        __CPROVER_assert(ref_carryable(out, &width, &sign),
                         "no adapter can pass an uncarryable type");
    else __CPROVER_assert(out == 0xAA, "port failure preserves output");
}

void h_no_metadata(void)
{
    uint8_t b[TS_GENERAL_DATA_BYTES];
    int32_t out = 0;
    result_t rc;
    b[0] = nondet_u8();
    b[1] = nondet_u8();
    rc = lab_typed_read(NULL, 0x7E, 0x01, TS_READ_LENIENT, b, sizeof b, &out);
    __CPROVER_assert(rc == RES_ENOTFOUND, "no metadata remains observable");
    __CPROVER_assert(out == (int32_t)((uint32_t)b[0] * 256u + b[1]),
                     "all raw byte pairs preserved without metadata");
}

static result_t supplied_type(const void *context, uint8_t sa, uint8_t sid, uint8_t *out)
{
    (void)sa;
    (void)sid;
    *out = *(const uint8_t *)context;
    return RES_OK;
}

void h_read(void)
{
    uint8_t dtype = nondet_u8();
    uint8_t b[TS_GENERAL_DATA_BYTES];
    lab_meta_port_t port = { supplied_type, &dtype };
    int32_t out = 0;
    int32_t want = 0;
    int accepted;
    result_t rc;
    b[0] = nondet_u8();
    b[1] = nondet_u8();
    rc = lab_typed_read(&port, 0x7E, 0x01, TS_READ_LENIENT, b, sizeof b, &out);
    accepted = ref_decode(dtype, REF_READ_LENIENT, b, &want);
    __CPROVER_assert((rc == RES_OK) == accepted, "typed path acceptance equals oracle");
    if (rc == RES_OK) __CPROVER_assert(out == want, "typed path value equals oracle");
    else __CPROVER_assert(out == (int32_t)((uint32_t)b[0] * 256u + b[1]),
                          "failed typed read keeps raw value");
}

void h_write(void)
{
    uint8_t dtype = nondet_u8();
    uint8_t b[TS_GENERAL_DATA_BYTES] = { 0x5A, 0xA5 };
    uint8_t want[TS_GENERAL_DATA_BYTES];
    int32_t value = nondet_i32();
    uint8_t layout = nondet_u8();
    lab_meta_port_t port = { supplied_type, &dtype };
    int accepted;
    result_t rc;
    __CPROVER_assume(layout <= (uint8_t)TS_LAYOUT_ZERO_FILL);
    rc = lab_typed_encode(&port, 0x7E, 0x01, (ts_layout_t)layout, value, b, sizeof b);
    accepted = ref_encode(dtype, (int)layout, value, want);
    __CPROVER_assert((rc == RES_OK) == accepted, "typed write acceptance equals oracle");
    if (rc == RES_OK)
        __CPROVER_assert(b[0] == want[0] && b[1] == want[1], "typed write bytes equal oracle");
    else __CPROVER_assert(b[0] == 0x5A && b[1] == 0xA5, "failed write preserves buffer");
}

void h_arguments(void)
{
    char line[LAB_META_LINE_BYTES];
    lab_meta_entry_t out;
    uint8_t b[TS_GENERAL_DATA_BYTES] = { 0x5A, 0xA5 };
    int32_t value = 123;
    size_t capacity = nondet_size();
    __CPROVER_assume(capacity < TS_GENERAL_DATA_BYTES);
    __CPROVER_assert(lab_meta_parse(NULL, sizeof line, &out) == RES_EINVAL,
                     "null line refused");
    __CPROVER_assert(lab_meta_parse(line, sizeof line, NULL) == RES_EINVAL,
                     "null parse destination refused");
    __CPROVER_assert(lab_typed_read(NULL, 0, 0, TS_READ_LENIENT, b, capacity,
                                   &value) == RES_EINVAL, "short read refused");
    __CPROVER_assert(value == 123, "short read leaves output untouched");
    __CPROVER_assert(lab_typed_encode(NULL, 0, 0, TS_LAYOUT_SIGN_EXTEND, 0,
                                     b, capacity) == RES_EINVAL, "short write refused");
    __CPROVER_assert(b[0] == 0x5A && b[1] == 0xA5, "short write preserves buffer");
}
