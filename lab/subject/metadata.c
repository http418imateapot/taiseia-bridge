/* SPDX-License-Identifier: Apache-2.0 */
#include "metadata.h"

static result_t hex_digit(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') *out = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') *out = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') *out = (uint8_t)(c - 'A' + 10);
    else return RES_EFORMAT;
    return RES_OK;
}

result_t lab_meta_parse(const char *line, size_t len, lab_meta_entry_t *out)
{
    uint8_t fields[3];
    size_t i;
    int32_t lo = 0;
    int32_t hi = 0;
    result_t rc;

    if (!line || !out) return RES_EINVAL;
    if (len != LAB_META_LINE_BYTES) return RES_EFORMAT;
    if (line[2] != ':' || line[5] != ':') return RES_EFORMAT;
    for (i = 0; i < sizeof fields; ++i) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (hex_digit(line[i * 3u], &high) != RES_OK ||
            hex_digit(line[i * 3u + 1u], &low) != RES_OK)
            return RES_EFORMAT;
        fields[i] = (uint8_t)((unsigned)high * 16u + low);
    }
    if (fields[1] >= 0x80u) return RES_EFORMAT;
    rc = ts_general_domain(fields[2], &lo, &hi);
    if (rc != RES_OK) return rc;
    out->sa_id = fields[0];
    out->service_id = fields[1];
    out->dtype = fields[2];
    return RES_OK;
}

result_t lab_meta_init(lab_meta_config_t *config)
{
    if (!config) return RES_EINVAL;
    config->count = 0;
    config->ready = false;
    return RES_OK;
}

result_t lab_meta_add(lab_meta_config_t *config, const char *line, size_t len)
{
    lab_meta_entry_t entry;
    size_t i;
    result_t rc;
    if (!config) return RES_EINVAL;
    if (config->count > LAB_META_CAPACITY) return RES_EINVAL;
    if (config->ready) return RES_EBUSY;
    rc = lab_meta_parse(line, len, &entry);
    if (rc != RES_OK) {
        config->count = LAB_META_CAPACITY + 1u;
        return rc;
    }
    for (i = 0; i < config->count; ++i) {
        if (config->entries[i].sa_id == entry.sa_id &&
            config->entries[i].service_id == entry.service_id) {
            config->count = LAB_META_CAPACITY + 1u;
            return RES_EFORMAT;
        }
    }
    if (config->count == LAB_META_CAPACITY) {
        config->count = LAB_META_CAPACITY + 1u;
        return RES_ETOOBIG;
    }
    config->entries[config->count] = entry;
    ++config->count;
    return RES_OK;
}

result_t lab_meta_finish(lab_meta_config_t *config)
{
    if (!config) return RES_EINVAL;
    if (config->count > LAB_META_CAPACITY) return RES_EFORMAT;
    config->ready = true;
    return RES_OK;
}

result_t lab_meta_config_lookup(const void *context, uint8_t sa, uint8_t sid,
                                 uint8_t *out)
{
    const lab_meta_config_t *c = (const lab_meta_config_t *)context;
    size_t i;
    if (!c || !out) return RES_EINVAL;
    if (!c->ready || c->count > LAB_META_CAPACITY) return RES_EFORMAT;
    for (i = 0; i < c->count; ++i) {
        if (c->entries[i].sa_id == sa && c->entries[i].service_id == sid) {
            *out = c->entries[i].dtype;
            return RES_OK;
        }
    }
    return RES_ENOTFOUND;
}

result_t lab_meta_registration_lookup(const void *context, uint8_t sa, uint8_t sid,
                                       uint8_t *out)
{
    const lab_meta_registration_t *r = (const lab_meta_registration_t *)context;
    size_t i;
    if (!r || !out || (!r->entries && r->count)) return RES_EINVAL;
    if (r->count > r->capacity) return RES_ETOOBIG;
    if (!r->has_wire_types) return RES_ENOTFOUND;
    for (i = 0; i < r->count; ++i) {
        if (r->entries[i].sa_id == sa && r->entries[i].service_id == sid) {
            *out = r->entries[i].dtype;
            return RES_OK;
        }
    }
    return RES_ENOTFOUND;
}

result_t lab_meta_lookup(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                         uint8_t *out)
{
    uint8_t dtype = 0;
    int32_t lo = 0;
    int32_t hi = 0;
    result_t rc;
    if (!out || sid >= 0x80u) return RES_EINVAL;
    if (!port || !port->lookup) return RES_ENOTFOUND;
    rc = port->lookup(port->context, sa, sid, &dtype);
    if (rc != RES_OK) {
        /* Third-party error vocabulary is bounded at this boundary. */
        if (rc == RES_ENOTFOUND || rc == RES_EUNSUPPORTED ||
            rc == RES_EFORMAT || rc == RES_ETOOBIG || rc == RES_EINVAL)
            return rc;
        return RES_EIO;
    }
    rc = ts_general_domain(dtype, &lo, &hi);
    if (rc != RES_OK) return rc;
    *out = dtype;
    return RES_OK;
}

result_t lab_typed_read(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                        ts_reading_t reading, const uint8_t *data, size_t len,
                        int32_t *out)
{
    uint8_t dtype = 0;
    result_t rc;
    if (!out || !data || len != TS_GENERAL_DATA_BYTES) return RES_EINVAL;
    *out = (int32_t)((uint32_t)data[0] * 256u + data[1]);
    rc = lab_meta_lookup(port, sa, sid, &dtype);
    if (rc != RES_OK) return rc;
    return ts_general_decode(dtype, reading, data, len, out);
}

result_t lab_typed_encode(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                          ts_layout_t layout, int32_t value,
                          uint8_t *out, size_t capacity)
{
    uint8_t dtype = 0;
    result_t rc;
    if (!out || capacity < TS_GENERAL_DATA_BYTES) return RES_EINVAL;
    rc = lab_meta_lookup(port, sa, sid, &dtype);
    if (rc != RES_OK) return rc;
    return ts_general_encode(dtype, layout, value, out, capacity);
}
