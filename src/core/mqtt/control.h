/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Inbound control commands.
 *
 * This lives in the core rather than in the application because it is the one
 * place a stranger's bytes turn into instructions for an appliance, and that
 * deserves to be a pure function with tests rather than a loop in main().
 *
 *     { "tag": <anything>, "attributes": { "3": 70, "0": 1 } }
 *
 * `tag` is copied out as raw bytes and never decoded, so it goes back in the
 * acknowledgement exactly as it arrived and still compares equal to what the
 * sender sent.
 */
#ifndef TSB_CORE_MQTT_CONTROL_H
#define TSB_CORE_MQTT_CONTROL_H

#include "core/model/appliance.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>

#define CONTROL_TAG_MAX     128u
#define CONTROL_MAX_TOKENS  96

typedef struct {
    char              tag[CONTROL_TAG_MAX];
    size_t            tag_len;
    bool              has_tag;

    size_t            n;
    appl_write_item_t items[APPL_WRITE_BATCH_MAX];
} control_cmd_t;

/**
 * Decode one command.
 *
 * RES_EFORMAT when the document is malformed, when `attributes` is missing or
 * empty, when a key is not a service identifier, or when a value will not fit
 * in one. RES_ETOOBIG when there are more attributes than a batch holds, or a
 * tag too large to echo.
 *
 * A tag that does not fit is refused rather than dropped: an acknowledgement
 * the sender cannot correlate is barely an acknowledgement, and silently
 * losing the correlation is worse than saying so.
 */
result_t control_parse(const char *payload, size_t len, control_cmd_t *out);

#endif /* TSB_CORE_MQTT_CONTROL_H */
