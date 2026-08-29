/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Data type codes.
 *
 * A service's values, and the shape of its declared capability, are both
 * governed by a one-byte type code that the appliance reports at registration
 * time. This module maps that code to the two things a decoder needs: how many
 * bytes a value occupies, and how to read the accompanying support data.
 *
 * This is a format-layer table only -- code to width. It says nothing about
 * what any particular service means; that belongs to the consumer, which holds
 * its own copy of the per-appliance service tables. See docs/adr/0003.
 */
#ifndef TSB_CORE_TAISEIA_DATATYPE_H
#define TSB_CORE_TAISEIA_DATATYPE_H

#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * How to read the support data that accompanies a capability.
 *
 * The distinction matters and is easy to get wrong: for an enumeration the
 * support bits say *which values are allowed*, whereas for a bit type they say
 * *which bits of the value are meaningful*. Both arrive as a mask; conflating
 * them produces a plausible and incorrect reading.
 */
typedef enum {
    TS_SUPPORT_RANGE = 0,  /* minimum in the high half, maximum in the low  */
    TS_SUPPORT_MAX,        /* maximum only; the minimum is fixed at zero    */
    TS_SUPPORT_BITS,       /* one bit per supported value or function       */
    TS_SUPPORT_COMPOSITE,  /* a timestamp followed by one of the above      */
    TS_SUPPORT_VARIABLE    /* string: the support datum is a maximum length */
} ts_support_kind_t;

typedef struct {
    uint8_t           code;
    const char       *name;
    size_t            value_len;    /* bytes; 0 when variable (string)      */
    size_t            support_len;  /* bytes; 0 when variable               */
    ts_support_kind_t support;
    bool              is_signed;
} ts_datatype_t;

/** Look up a type code. NULL when the code is not one we know. */
const ts_datatype_t *ts_datatype(uint8_t code);

/** Name for logging and for the `dtype` field of published capabilities. */
const char *ts_datatype_name(uint8_t code);

/** Table walk, for tests and for tooling that wants to enumerate support. */
size_t               ts_datatype_count(void);
const ts_datatype_t *ts_datatype_at(size_t i);

/**
 * Decode a big-endian integer of 1, 2, 4 or 8 bytes.
 *
 * Sign extension is applied when `is_signed`, because the protocol represents
 * negative numbers in two's complement. Reading every value as unsigned is the
 * single most likely decoding mistake here: it turns -3 into 65533 without any
 * outward sign that something went wrong.
 */
result_t ts_decode_int(const uint8_t *data, size_t n, bool is_signed,
                       int64_t *out);

#endif /* TSB_CORE_TAISEIA_DATATYPE_H */
