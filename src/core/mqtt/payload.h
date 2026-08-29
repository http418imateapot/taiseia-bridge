/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The published payloads.
 *
 * Every builder writes into a caller-supplied strbuf and reports whether the
 * whole document fitted. Nothing is published half-written: the string
 * builder's overflow is sticky, so a truncated document is never mistaken for
 * a short one.
 *
 * TIME IS A STRING PARAMETER. The core has no clock, so the caller formats the
 * timestamp and passes it in. NULL emits null, which is what a last-will
 * message wants: the broker publishes it at an unknown later moment, and a
 * timestamp taken now would be a lie about when the device went away.
 *
 * Values and identifiers go out raw. Interpreting them needs the per-appliance
 * service tables, which are not in this project -- see docs/adr/0003.
 */
#ifndef TSB_CORE_MQTT_PAYLOAD_H
#define TSB_CORE_MQTT_PAYLOAD_H

#include "core/model/appliance.h"
#include "core/util/result.h"
#include "core/util/strbuf.h"

#include <stdbool.h>
#include <stddef.h>

#define WRITE_CODE_COUNT 5   /* one per appl_write_result_t */

/*
 * How a write outcome is rendered.
 *
 * The device protocol has no write result codes, so there is no natural
 * numbering to inherit and this project declines to invent one. The default is
 * therefore the outcome's name: a consumer reading "not_taken" needs no
 * lookup table. A deployment whose backend expects particular numbers sets
 * them here, in its configuration, where they are a property of that
 * deployment rather than of this code. See docs/adr/0011.
 */
typedef struct {
    bool numeric;
    int  code[WRITE_CODE_COUNT];
} write_code_map_t;

void        write_code_map_defaults(write_code_map_t *m);
const char *write_result_name(appl_write_result_t r);

/** Current values of the appliance's services. */
result_t payload_telemetry(strbuf_t *sb, const char *device_id,
                           const char *iso_time, const appliance_t *a);

/** What the appliance says it can do, as it reported it. */
result_t payload_capabilities(strbuf_t *sb, const char *device_id,
                              const char *iso_time, const appliance_t *a);

/** Device liveness. Pass iso_time NULL for the last will. */
result_t payload_status(strbuf_t *sb, const char *device_id,
                        const char *iso_time, bool online);

/**
 * Acknowledgement for one control command.
 *
 * `tag_raw` is echoed back byte for byte, whatever its JSON type, so the
 * sender's own correlation value comes back unchanged rather than decoded and
 * re-encoded into something that no longer compares equal.
 */
result_t payload_ack(strbuf_t *sb, const char *device_id, const char *iso_time,
                     const char *tag_raw, size_t tag_len,
                     const appl_write_batch_t *batch,
                     const write_code_map_t *map);

/** Acknowledgement for a command that could not be attempted at all. */
result_t payload_ack_error(strbuf_t *sb, const char *device_id,
                           const char *iso_time,
                           const char *tag_raw, size_t tag_len,
                           const char *error);

#endif /* TSB_CORE_MQTT_PAYLOAD_H */
