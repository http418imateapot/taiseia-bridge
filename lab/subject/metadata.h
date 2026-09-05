/* SPDX-License-Identifier: Apache-2.0
 * R21 research subject. Nothing under src/ depends on this file.
 * Input: exactly AA:SS:TT (hex bytes), no Annex A service table.
 */
#ifndef TSB_LAB_METADATA_H
#define TSB_LAB_METADATA_H
#include "core/taiseia/general.h"
#include <stdbool.h>

#define LAB_META_CAPACITY 4u
#define LAB_META_LINE_BYTES 8u

typedef struct {
    uint8_t sa_id;
    uint8_t service_id;
    uint8_t dtype;
} lab_meta_entry_t;

typedef struct {
    lab_meta_entry_t entries[LAB_META_CAPACITY];
    size_t count;
    bool ready;
} lab_meta_config_t;

typedef struct {
    const lab_meta_entry_t *entries;
    size_t count;
    size_t capacity;
    bool has_wire_types;
} lab_meta_registration_t;

typedef struct {
    result_t (*lookup)(const void *, uint8_t, uint8_t, uint8_t *);
    const void *context;
} lab_meta_port_t;

result_t lab_meta_parse(const char *line, size_t len, lab_meta_entry_t *out);
result_t lab_meta_init(lab_meta_config_t *config);
result_t lab_meta_add(lab_meta_config_t *config, const char *line, size_t len);
result_t lab_meta_finish(lab_meta_config_t *config);
result_t lab_meta_config_lookup(const void *context, uint8_t sa, uint8_t sid,
                                 uint8_t *out);
result_t lab_meta_registration_lookup(const void *context, uint8_t sa, uint8_t sid,
                                       uint8_t *out);
result_t lab_meta_lookup(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                         uint8_t *out);
/* On metadata/format failure read preserves the unsigned raw value AND error.
 * Writes never invent a type; failure leaves the caller's buffer untouched. */
result_t lab_typed_read(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                        ts_reading_t reading, const uint8_t *data, size_t len,
                        int32_t *out);
result_t lab_typed_encode(const lab_meta_port_t *port, uint8_t sa, uint8_t sid,
                          ts_layout_t layout, int32_t value,
                          uint8_t *out, size_t capacity);
#endif
