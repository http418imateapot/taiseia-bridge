/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reassembling a reply that arrived in pieces.
 *
 * A reply too long for one packet is sent as several, numbered downwards, with
 * zero marking the last. Each piece is a complete, checksummed packet; only
 * the payloads join up.
 *
 * WHAT THE STANDARD SETTLES, AND WHAT IT DOES NOT.
 *
 * For a multi-byte reply it is explicit: every fragment carries the same
 * header, its own fragment number at bytes 4 and 5, and the count of the items
 * *in that fragment*. Concatenating the data fields and summing the counts is
 * the whole of it.
 *
 * For a registration reply it is not. The fragment number lives inside the
 * payload, at a fixed offset within a header that a pure continuation would
 * not have -- so a continuation cannot be pure, or the number could not be
 * read. The reading implemented here is the only one that is self-consistent:
 * every fragment repeats the fixed header, and what follows it is what joins
 * up. It is an inference, it is marked as one wherever it matters, and only a
 * real appliance can confirm it. See docs/06 in the open questions.
 */
#ifndef TSB_CORE_TAISEIA_REASSEMBLY_H
#define TSB_CORE_TAISEIA_REASSEMBLY_H

#include "core/taiseia/frame.h"
#include "core/taiseia/limits.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    ts_reply_kind_t kind;

    uint8_t  data[TS_REASSEMBLY_MAX];
    size_t   len;

    uint32_t count;        /* items, summed across fragments   */
    uint8_t  dtype;
    uint8_t  sa_id;
    uint8_t  service_id;
    bool     write;

    unsigned fragments;
    uint16_t last_fragment;
    bool     started;
    bool     complete;
} ts_reasm_t;

/** Start gathering a reply of the given kind. */
void ts_reasm_begin(ts_reasm_t *r, ts_reply_kind_t kind);

/**
 * Add one complete packet.
 *
 * RES_OK once the last fragment has been taken -- check ts_reasm_complete.
 * RES_EAGAIN when more are expected.
 *
 * Refuses rather than assembles something plausible: a fragment number that
 * does not count down, more bytes than the buffer holds, a packet that does
 * not decode, or a continuation that is missing the header it must repeat.
 */
result_t ts_reasm_add(ts_reasm_t *r, const uint8_t *pkt, size_t len);

bool ts_reasm_complete(const ts_reasm_t *r);

#endif /* TSB_CORE_TAISEIA_REASSEMBLY_H */
