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

/* -------------------------------------------------------- service and role --
 *
 * Byte 2 packs the access direction into the top bit and the service id into
 * the remaining seven.
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

#endif /* TSB_CORE_TAISEIA_LIMITS_H */
