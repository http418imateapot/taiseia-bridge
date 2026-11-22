/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The registration exchange: how an appliance describes itself.
 *
 * A request with SA id 0x00 and service id 0x00 asks the appliance who it is.
 * The reply carries its device class, protocol version, appliance type, make,
 * model, and one record per service it supports. This is the only source of
 * capability information in the project -- there are no service tables here,
 * by design. See docs/adr/0003-no-service-tables.md.
 *
 * Reply payload layout (the frame's data field, byte 2 onward):
 *
 *     [0]     device class: bit 7 selects the record format below,
 *             bits 3..0 the class, bits 6..4 reserved
 *     [1]     protocol version, major
 *     [2]     protocol version, minor
 *     [3]     bits 7..4 fragment number, bits 3..0 link speed code
 *     [4..5]  appliance type (SA id), big endian
 *     [6..]   make, NUL terminated
 *             model, NUL terminated
 *             one capability record per service, to the end of the payload
 *
 * TWO RECORD FORMATS, and the difference matters more than it looks.
 *
 *   General (3 bytes)     [service | write bit] [support high] [support low]
 *   Multi-byte (4+N)      [type: write bit] [service id, 2 bytes] [type code]
 *                         [support ...]
 *
 * Only the multi-byte format carries a data type code. For a general-format
 * appliance the two support bytes could be a range, a maximum, or a bit mask,
 * and nothing on the wire says which: that fact lives in the licensed service
 * tables. So `dtype_known` is false for those, `support` is handed over as the
 * raw two bytes, and interpreting it is the consumer's job. Guessing here
 * would produce a confident, wrong reading.
 */
#ifndef TSB_CORE_TAISEIA_REGISTRATION_H
#define TSB_CORE_TAISEIA_REGISTRATION_H

#include "core/taiseia/limits.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Make and model are NUL-terminated strings. The standard bounds each below
 * twenty characters; the buffer is larger so that a device slightly over the
 * limit is still readable, and a missing terminator is refused rather than
 * having one invented for it. */
#define TS_STRING_MAX          31u

/* The widest support datum any known type declares: a six-byte timestamp
 * followed by a 64-bit range. Checked against the type table by a test. */
#define TS_SUPPORT_MAX_BYTES   22u

/* Fixed part of the payload, before make and model. */
#define TS_REG_HEADER_LEN      6u

#define TS_REG_GENERAL_RECORD  3u   /* service byte + two support bytes */

/*
 * A multi-byte record, CNS 16014:2025 table 24: a service type byte (bit 7 is
 * the access direction, bits 6..0 reserved), a two-byte service id, the data
 * type code, then the support data, whose width the type code decides. This
 * is what precedes the support data.
 */
#define TS_REG_MB_RECORD_HEADER 4u
#define TS_REG_MB_OFF_ID        1u  /* two bytes, big endian */
#define TS_REG_MB_OFF_DTYPE     3u

typedef enum {
    TS_FORMAT_GENERAL = 0,   /* 3-byte records, no type code            */
    TS_FORMAT_MULTIBYTE      /* type code present, variable support data */
} ts_format_t;

/* Device class, from the low nibble of payload byte 0. */
#define TS_CLASS_APPLIANCE   0u
#define TS_CLASS_GENERATION  1u
#define TS_CLASS_STORAGE     2u
#define TS_CLASS_SENSOR      3u

typedef struct {
    uint16_t service_id;                     /* 7 bits general, 16 multi-byte */
    bool     writable;
    bool     dtype_known;                    /* multi-byte format only     */
    uint8_t  dtype;                          /* valid when dtype_known     */
    uint8_t  support[TS_SUPPORT_MAX_BYTES];
    size_t   support_len;
} ts_capability_t;

typedef struct {
    ts_format_t     format;
    uint8_t         device_class;
    uint8_t         proto_major;
    uint8_t         proto_minor;
    uint8_t         fragment;    /* non-zero means this reply is one piece */
    uint8_t         baud_code;
    uint16_t        sa_id;       /* appliance type; see the standard's list */
    char            make[TS_STRING_MAX + 1u];
    char            model[TS_STRING_MAX + 1u];
    size_t          n_caps;
    ts_capability_t caps[TS_MAX_CAPABILITIES];
} ts_registration_t;

/**
 * Decode a registration reply payload.
 *
 * `data` is the frame's data field -- what `ts_parse_response` hands back for
 * a reply with SA id 0x00, not the whole packet.
 *
 * Returns:
 *   RES_EFORMAT       malformed: short header, unterminated string, a record
 *                     stream that does not divide evenly, or more services
 *                     than TS_MAX_CAPABILITIES
 *   RES_EUNSUPPORTED  well formed but not decodable here: a fragmented reply,
 *                     or a record whose type code has no fixed support width.
 *                     The header fields are filled in before returning, so a
 *                     caller can log which case it hit.
 */
result_t ts_registration_parse(const uint8_t *data, size_t len,
                               ts_registration_t *out);

/** Link speed in bits per second, or 0 when the code is not one we know. */
uint32_t ts_baud_from_code(uint8_t code);

/**
 * Find a capability by service id; NULL when the appliance did not offer it.
 *
 * The id is compared whole. 0x0120 and 0x20 are different services, and a
 * general-format appliance, whose ids are seven bits, offers neither 0x0120
 * nor anything else above 0x7F.
 */
const ts_capability_t *ts_registration_find(const ts_registration_t *reg,
                                            uint16_t service_id);

#endif /* TSB_CORE_TAISEIA_REGISTRATION_H */
