/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/payload.h"

#include "core/taiseia/datatype.h"
#include "core/util/jsonw.h"

void write_code_map_defaults(write_code_map_t *m)
{
    int i;

    if (!m)
        return;

    m->numeric = false;
    for (i = 0; i < WRITE_CODE_COUNT; i++)
        m->code[i] = i;
}

const char *write_result_name(appl_write_result_t r)
{
    switch (r) {
    case APPL_WRITE_OK:        return "ok";
    case APPL_WRITE_UNKNOWN:   return "unknown";
    case APPL_WRITE_READ_ONLY: return "read_only";
    case APPL_WRITE_NOT_TAKEN: return "not_taken";
    case APPL_WRITE_NO_REPLY:  return "no_reply";
    }
    return "unknown";
}

/* device_id, device_time -- on every device-originated payload. */
static void envelope(jsonw_t *w, const char *device_id, const char *iso_time)
{
    jw_kstr(w, "device_id", device_id);
    if (iso_time)
        jw_kstr(w, "device_time", iso_time);
    else
        jw_knull(w, "device_time");
}

static const char *format_name(const appliance_t *a)
{
    return (a->reg.format == TS_FORMAT_MULTIBYTE) ? "multibyte" : "general";
}

static result_t finish(jsonw_t *w)
{
    return jw_ok(w) ? RES_OK : RES_ETOOBIG;
}

result_t payload_status(strbuf_t *sb, const char *device_id,
                        const char *iso_time, bool online)
{
    jsonw_t w;

    if (!sb || !device_id)
        return RES_EINVAL;

    sb_reset(sb);
    jw_init(&w, sb);

    jw_obj_open(&w);
    envelope(&w, device_id, iso_time);
    jw_kbool(&w, "online", online);
    jw_obj_close(&w);

    return finish(&w);
}

/*
 * One attribute.
 *
 * The general format has a two-byte value and nothing on the wire to say
 * whether it is signed, so it goes out unsigned. The multi-byte format brings
 * a type and a count with it, so a single item becomes a number -- signed if
 * the type says so -- and several become an array.
 *
 * Anything whose width this cannot decode goes out as hex rather than as a
 * guess: a six-byte timestamp, a composite, a string whose encoding is not
 * ours to assume.
 */
static void put_attribute(jsonw_t *w, const appl_value_t *v)
{
    const ts_datatype_t *t;
    size_t               items;
    size_t               i;

    jw_key_uint(w, v->service_id);

    if (!v->multibyte) {
        jw_uint(w, v->value);
        return;
    }

    t = ts_datatype(v->dtype);
    if (!t || t->value_len == 0 || v->raw_len == 0 ||
        v->raw_len % t->value_len != 0) {
        jw_hex(w, v->raw, v->raw_len);
        return;
    }

    items = v->raw_len / t->value_len;

    if (items == 1) {
        int64_t x = 0;

        if (ts_decode_int(v->raw, t->value_len, t->is_signed, &x) != RES_OK)
            jw_hex(w, v->raw, v->raw_len);
        else if (t->is_signed)
            jw_int(w, x);
        else
            jw_uint(w, (uint64_t)x);
        return;
    }

    jw_arr_open(w);
    for (i = 0; i < items; i++) {
        int64_t x = 0;

        if (ts_decode_int(v->raw + i * t->value_len, t->value_len,
                          t->is_signed, &x) != RES_OK)
            jw_hex(w, v->raw + i * t->value_len, t->value_len);
        else if (t->is_signed)
            jw_int(w, x);
        else
            jw_uint(w, (uint64_t)x);
    }
    jw_arr_close(w);
}

result_t payload_telemetry(strbuf_t *sb, const char *device_id,
                           const char *iso_time, const appliance_t *a)
{
    jsonw_t w;
    size_t  i;
    bool    online;

    if (!sb || !device_id || !a)
        return RES_EINVAL;

    online = appl_online(a);

    sb_reset(sb);
    jw_init(&w, sb);

    jw_obj_open(&w);
    envelope(&w, device_id, iso_time);
    if (online)
        jw_kuint(&w, "sa_id", a->reg.sa_id);
    else
        jw_knull(&w, "sa_id");
    jw_kbool(&w, "online", online);

    jw_key(&w, "attributes");
    jw_obj_open(&w);
    if (online) {
        for (i = 0; i < a->n_values; i++) {
            if (!a->values[i].valid)
                continue;
            put_attribute(&w, &a->values[i]);
        }
    }
    jw_obj_close(&w);

    jw_obj_close(&w);

    return finish(&w);
}

/*
 * Render one capability's support datum.
 *
 * Only the multi-byte record format carries a data type code. Without it the
 * two support bytes could be a range, a maximum or a bit mask, and the wire
 * does not say which -- so the general format gets raw hex and no guess. See
 * docs/adr/0003.
 */
static void put_support(jsonw_t *w, const ts_capability_t *c)
{
    const ts_datatype_t *t = NULL;

    if (c->dtype_known)
        t = ts_datatype(c->dtype);

    jw_key(w, "support");
    jw_obj_open(w);

    if (!t || c->support_len == 0) {
        jw_kstr(w, "kind", "raw");
        jw_khex(w, "hex", c->support, c->support_len);
        jw_obj_close(w);
        return;
    }

    switch (t->support) {
    case TS_SUPPORT_RANGE: {
        size_t  half = c->support_len / 2u;
        int64_t lo = 0, hi = 0;

        /* The first half is the minimum, the second the maximum. Reading them
         * the other way round is the obvious mistake and produces a range that
         * still looks like a range. */
        if (half > 0 &&
            ts_decode_int(c->support, half, t->is_signed, &lo) == RES_OK &&
            ts_decode_int(c->support + half, half, t->is_signed, &hi)
                == RES_OK) {
            jw_kstr(w, "kind", "range");
            jw_kint(w, "min", lo);
            jw_kint(w, "max", hi);
            break;
        }
        goto raw;
    }

    case TS_SUPPORT_MAX: {
        int64_t hi = 0;

        if (ts_decode_int(c->support, c->support_len, t->is_signed, &hi)
            == RES_OK) {
            jw_kstr(w, "kind", "max");
            jw_kint(w, "min", 0);
            jw_kint(w, "max", hi);
            break;
        }
        goto raw;
    }

    case TS_SUPPORT_BITS: {
        int64_t mask = 0;

        if (ts_decode_int(c->support, c->support_len, false, &mask)
            == RES_OK) {
            jw_kstr(w, "kind", "bits");
            jw_kint(w, "mask", mask);
            jw_kuint(w, "width", (uint64_t)c->support_len * 8u);
            break;
        }
        goto raw;
    }

    case TS_SUPPORT_COMPOSITE:
    case TS_SUPPORT_VARIABLE:
    default:
        goto raw;
    }

    jw_obj_close(w);
    return;

raw:
    /* A width ts_decode_int cannot represent -- a six-byte timestamp, a
     * composite, a string. Handing over the bytes beats inventing a shape. */
    jw_kstr(w, "kind", "raw");
    jw_khex(w, "hex", c->support, c->support_len);
    jw_obj_close(w);
}

result_t payload_capabilities(strbuf_t *sb, const char *device_id,
                              const char *iso_time, const appliance_t *a)
{
    jsonw_t w;
    size_t  i;
    bool    online;

    if (!sb || !device_id || !a)
        return RES_EINVAL;

    online = appl_online(a);

    sb_reset(sb);
    jw_init(&w, sb);

    jw_obj_open(&w);
    envelope(&w, device_id, iso_time);
    if (online)
        jw_kuint(&w, "sa_id", a->reg.sa_id);
    else
        jw_knull(&w, "sa_id");
    jw_kbool(&w, "online", online);

    if (online) {
        jw_kstr(&w, "format", format_name(a));
        jw_kstr(&w, "make", a->reg.make);
        jw_kstr(&w, "model", a->reg.model);
    }

    jw_key(&w, "capabilities");
    jw_arr_open(&w);
    if (online) {
        for (i = 0; i < a->reg.n_caps; i++) {
            const ts_capability_t *c = &a->reg.caps[i];

            jw_obj_open(&w);
            jw_kuint(&w, "id", c->service_id);
            jw_kstr(&w, "access", c->writable ? "rw" : "r");
            if (c->dtype_known)
                jw_kstr(&w, "dtype", ts_datatype_name(c->dtype));
            put_support(&w, c);
            jw_obj_close(&w);
        }
    }
    jw_arr_close(&w);

    jw_obj_close(&w);

    return finish(&w);
}

static void put_write_result(jsonw_t *w, appl_write_result_t r,
                             const write_code_map_t *map)
{
    if (map && map->numeric) {
        int idx = (int)r;

        if (idx < 0 || idx >= WRITE_CODE_COUNT)
            idx = 0;
        jw_int(w, map->code[idx]);
        return;
    }
    jw_str(w, write_result_name(r));
}

result_t payload_ack(strbuf_t *sb, const char *device_id, const char *iso_time,
                     const char *tag_raw, size_t tag_len,
                     const appl_write_batch_t *batch,
                     const write_code_map_t *map)
{
    jsonw_t w;
    size_t  i;
    bool    success = true;

    if (!sb || !device_id || !batch)
        return RES_EINVAL;

    for (i = 0; i < batch->n; i++) {
        if (batch->items[i].result != APPL_WRITE_OK) {
            success = false;
            break;
        }
    }

    sb_reset(sb);
    jw_init(&w, sb);

    jw_obj_open(&w);
    jw_kbool(&w, "success", success);
    jw_kraw(&w, "tag", tag_raw, tag_len);
    envelope(&w, device_id, iso_time);

    jw_key(&w, "results");
    jw_obj_open(&w);
    for (i = 0; i < batch->n; i++) {
        jw_key_uint(&w, batch->items[i].service_id);
        put_write_result(&w, batch->items[i].result, map);
    }
    jw_obj_close(&w);

    jw_obj_close(&w);

    return finish(&w);
}

result_t payload_ack_error(strbuf_t *sb, const char *device_id,
                           const char *iso_time,
                           const char *tag_raw, size_t tag_len,
                           const char *error)
{
    jsonw_t w;

    if (!sb || !device_id || !error)
        return RES_EINVAL;

    sb_reset(sb);
    jw_init(&w, sb);

    jw_obj_open(&w);
    jw_kbool(&w, "success", false);
    jw_kraw(&w, "tag", tag_raw, tag_len);
    envelope(&w, device_id, iso_time);
    jw_kstr(&w, "error", error);
    jw_obj_close(&w);

    return finish(&w);
}
