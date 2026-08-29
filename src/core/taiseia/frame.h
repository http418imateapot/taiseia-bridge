/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Packet encoding and decoding for the appliance link. Pure functions: no
 * I/O, no allocation, no static state. Everything here is exercised by the
 * host test suite.
 *
 * Wire layout of a general-data-format packet:
 *
 *     byte 0        packet length, including this byte and the checksum
 *     byte 1        SA id (appliance type; 0x00 means registration)
 *     byte 2        bit 7 = write, bits 6..0 = service id
 *     byte 3..n-2   data
 *     byte n-1      checksum: XOR of every preceding byte
 *
 * A request in this format is always six bytes, so its data field is the two
 * bytes 3 and 4. A response carries as many data bytes as its length says.
 *
 * One exception, and it is why requests and responses are parsed by separate
 * functions rather than one. A registration *request* carries SA id 0x00 and
 * service id 0x00 in the usual places. A registration *response* has no
 * service byte at all: byte 2 is already the first byte of its payload. The
 * two are indistinguishable from their bytes alone, so the caller says which
 * it is holding.
 */
#ifndef TSB_CORE_TAISEIA_FRAME_H
#define TSB_CORE_TAISEIA_FRAME_H

#include "core/taiseia/limits.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t        sa_id;
    uint8_t        service_id;   /* write bit stripped; 0 when registration */
    bool           write;        /* bit 7 of the service byte               */
    bool           registration; /* SA id 0x00: no service byte in a reply  */
    const uint8_t *data;         /* points into the caller's buffer         */
    size_t         data_len;
} ts_frame_t;

/** XOR of buf[0 .. n-1]. The checksum byte itself is never included. */
uint8_t ts_checksum(const uint8_t *buf, size_t n);

/**
 * Build a read request for one service.
 *
 * Both data bytes are filled with 0xFF, which is how the protocol marks a
 * request that carries no value.
 */
result_t ts_build_read(uint8_t *out, size_t cap, size_t *out_len,
                       uint8_t sa_id, uint8_t service_id);

/**
 * Build a write request for one service.
 *
 * `value` is written big-endian across the two data bytes. A value that is
 * semantically one byte wide belongs in the low half with a zero high half;
 * this function does not know the service's data type and does not guess.
 */
result_t ts_build_write(uint8_t *out, size_t cap, size_t *out_len,
                        uint8_t sa_id, uint8_t service_id, uint16_t value);

/**
 * Read the declared total length from the first byte of a packet.
 *
 * This is what lets a transport read a header, learn the exact size, and then
 * read precisely that many bytes. Issuing one read and hoping the whole packet
 * arrived leaves the tail of a split packet in the buffer, which offsets every
 * later exchange from its reply with nothing to detect it.
 *
 * `have` may be as little as one byte. Returns RES_EAGAIN when the buffer is
 * still empty, and RES_EFORMAT when the declared length is a reserved value.
 */
result_t ts_frame_declared_len(const uint8_t *buf, size_t have, size_t *need);

/**
 * Decode a complete request packet.
 *
 * Byte 2 is always the service byte, registration included. Validates, in
 * order: the declared length against `len`, the declared length against the
 * reserved range, and the checksum. `out->data` aliases `buf`, so it stays
 * valid only as long as the caller's buffer does.
 */
result_t ts_parse_request(const uint8_t *buf, size_t len, ts_frame_t *out);

/**
 * Decode a complete response packet.
 *
 * Identical to `ts_parse_request` except for SA id 0x00, where there is no
 * service byte and the payload starts at byte 2. Parsing a registration reply
 * with the request layout silently drops its first payload byte and shifts
 * every field after it, so the distinction is not cosmetic.
 */
result_t ts_parse_response(const uint8_t *buf, size_t len, ts_frame_t *out);

#endif /* TSB_CORE_TAISEIA_FRAME_H */
