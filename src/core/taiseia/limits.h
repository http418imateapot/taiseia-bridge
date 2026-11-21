/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single source of truth for every compile-time limit in the protocol layer.
 *
 * RULE: buffer sizes are DERIVED here and referenced by name at the point of
 * declaration. Never hand-compute a size next to the buffer it bounds -- the
 * two copies drift, and the one that drifts is the one that overflows.
 */
#ifndef TSB_CORE_TAISEIA_LIMITS_H
#define TSB_CORE_TAISEIA_LIMITS_H

/* ---------------------------------------------------------------- framing --
 *
 * Byte 0 of every packet is the packet length, counted in bytes and including
 * byte 0 itself and the trailing checksum. The values 0..5 and 255 are
 * reserved, so a well-formed packet is 6..254 bytes long.
 */
#define TS_FRAME_MIN_LEN        6u
#define TS_FRAME_MAX_LEN        254u

/* length(1) + SA id(1) + service byte(1) + checksum(1) */
#define TS_FRAME_OVERHEAD       4u

/* A registration reply has no service byte, so its payload starts one byte
 * earlier and is one byte longer than any other reply of the same size. */
#define TS_REG_FRAME_OVERHEAD   3u

/* Sized for the longest data field any reply can carry, which is the
 * registration case. Sizing this from TS_FRAME_OVERHEAD instead would be one
 * byte short, and short by exactly the packet that matters most. */
#define TS_DATA_MAX             (TS_FRAME_MAX_LEN - TS_REG_FRAME_OVERHEAD)

/* A general-data-format request is always exactly this long: the two data
 * bytes are present whether or not the operation carries a value. */
#define TS_GENERAL_REQ_LEN      6u

/* ------------------------------------------------- multi-byte data format --
 *
 * A different packet layout, used by appliances that report values wider than
 * two bytes -- meters and sensors. The header is longer and carries the data
 * type and item count that the general format has nowhere to put. Offsets are
 * those of CNS 16014:2025 tables 14 (request) and 15 (response), which share
 * one layout:
 *
 *   [0] length  [1] SA id  [2] service type: bit 7 R/W, bits 6..0 reserved
 *   [3..4] service id  [5..6] reserved  [7..8] fragment  [9] data type
 *   [10..11] item count  [12..] data  [N-1] xor
 *
 * Unlike the general format, byte 2 does not hold the service id: the id has
 * two bytes of its own. Every multi-byte field is big endian.
 */
#define TS_MB_OFF_SERVICE_TYPE  2u
#define TS_MB_OFF_SERVICE_ID    3u
#define TS_MB_OFF_RESERVED      5u
#define TS_MB_OFF_FRAGMENT      7u
#define TS_MB_OFF_DTYPE         9u
#define TS_MB_OFF_COUNT         10u
#define TS_MB_DATA_OFFSET       12u
#define TS_MB_OVERHEAD          13u   /* header + checksum */

/* What we send in the reserved fields. The standard's default for the
 * two-byte field is 0x0000, and bits 6..0 of the service type byte are 0. */
#define TS_MB_RESERVED          0x0000u

/* The count field of a read request: we are asking, not telling. */
#define TS_MB_COUNT_UNSPECIFIED 0xFFFFu

/*
 * A reply too long for one packet arrives in several, numbered downwards, and
 * they are reassembled before anything above the transport sees them. The
 * ceiling is ours, not the protocol's: a device sending more than this is
 * refused rather than truncated, because a truncated capability list or a
 * truncated batch of readings looks plausible and is wrong.
 */
#define TS_REASSEMBLY_MAX       1024u

/*
 * The most we will hold for one service's value. A multi-byte appliance can
 * answer a batch read with many items; past this the reading is refused rather
 * than truncated, because a short batch looks exactly like a complete one.
 */
#define TS_VALUE_MAX_BYTES      64u

/* -------------------------------------------------------- service and role --
 *
 * In the general format, byte 2 packs the access direction into the top bit
 * and the service id into the remaining seven. The multi-byte format keeps the
 * direction in the same bit of the same byte and moves the id out to two bytes
 * of its own; see above.
 *
 * Everything above the frame layer -- capabilities, the appliance model, the
 * link port, MQTT -- holds a service id in seven bits. A multi-byte id wider
 * than that is refused where it enters, never truncated into another service.
 */
#define TS_SVC_ID_MASK          0x7Fu
#define TS_SVC_WRITE_BIT        0x80u
#define TS_SVC_ID_MAX           TS_SVC_ID_MASK

/* SA id 0x00 is reserved for the registration exchange, through which an
 * appliance reports its own class, protocol version, make, model and the set
 * of services it supports. Every other id denotes an appliance type. */
#define TS_SA_ID_REGISTRATION   0x00u

/* The filler written into both data bytes of a read request. */
#define TS_READ_FILL            0xFFu

/* ----------------------------------------------------------------- timing --
 *
 * An appliance must begin responding within 200 ms; beyond that the exchange
 * has timed out and the requester retries, at most three times. A gap of more
 * than 30 ms between two bytes of one packet invalidates that packet.
 */
#define TS_RESPONSE_TIMEOUT_MS  200
#define TS_INTERBYTE_MAX_MS     30
#define TS_MAX_RETRIES          3

/* --------------------------------------------------------- capability set --
 *
 * The largest capability list we will hold in memory. This is our own storage
 * ceiling, not a protocol limit: an appliance declaring more services than
 * this is refused outright rather than silently truncated, because a truncated
 * capability list looks plausible and is wrong.
 */
#define TS_MAX_CAPABILITIES     64u

/* ----------------------------------------------------- compile-time checks --
 *
 * C99 has no _Static_assert, so the negative-array-size trick stands in.
 */
#define TS_STATIC_ASSERT(cond, tag) \
    typedef char tsb_static_assert_##tag[(cond) ? 1 : -1]

TS_STATIC_ASSERT(TS_FRAME_MIN_LEN > TS_FRAME_OVERHEAD,   min_len_holds_data);
TS_STATIC_ASSERT(TS_FRAME_MAX_LEN < 255u,                max_len_reserves_ff);
TS_STATIC_ASSERT(TS_GENERAL_REQ_LEN >= TS_FRAME_MIN_LEN, general_req_is_valid);
TS_STATIC_ASSERT(TS_DATA_MAX > 0,                        data_max_positive);
TS_STATIC_ASSERT((TS_SVC_ID_MASK & TS_SVC_WRITE_BIT) == 0, service_byte_split);
TS_STATIC_ASSERT(TS_REG_FRAME_OVERHEAD < TS_FRAME_OVERHEAD,  reg_has_no_svc_byte);
TS_STATIC_ASSERT(TS_DATA_MAX >= TS_FRAME_MAX_LEN - TS_FRAME_OVERHEAD,
                                                             data_max_covers_all);
TS_STATIC_ASSERT(TS_MB_OVERHEAD > TS_FRAME_MIN_LEN,          mb_header_is_longer);
TS_STATIC_ASSERT(TS_MB_DATA_OFFSET + 1u == TS_MB_OVERHEAD,   mb_offsets_agree);
TS_STATIC_ASSERT(TS_MB_OFF_SERVICE_ID == TS_MB_OFF_SERVICE_TYPE + 1u,
                                                             mb_id_follows_type);
TS_STATIC_ASSERT(TS_MB_OFF_RESERVED == TS_MB_OFF_SERVICE_ID + 2u, mb_id_is_two);
TS_STATIC_ASSERT(TS_MB_OFF_FRAGMENT == TS_MB_OFF_RESERVED + 2u,   mb_rsv_is_two);
TS_STATIC_ASSERT(TS_MB_OFF_DTYPE == TS_MB_OFF_FRAGMENT + 2u,      mb_frag_is_two);
TS_STATIC_ASSERT(TS_MB_OFF_COUNT == TS_MB_OFF_DTYPE + 1u,         mb_dtype_is_one);
TS_STATIC_ASSERT(TS_MB_DATA_OFFSET == TS_MB_OFF_COUNT + 2u,       mb_count_is_two);
TS_STATIC_ASSERT(TS_REASSEMBLY_MAX >= TS_DATA_MAX,           reassembly_holds_one);

#endif /* TSB_CORE_TAISEIA_LIMITS_H */
