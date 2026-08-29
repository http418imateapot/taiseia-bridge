/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A JSON writer over a strbuf.
 *
 * It tracks nesting so that commas land in the right places and the caller
 * never writes punctuation. It does not validate that keys appear only inside
 * objects or that every open is closed -- that is the caller's business, and
 * checking it would cost more than it catches in code this size.
 *
 * Overflow is inherited from the strbuf: sticky, and checked once at the end
 * with jw_ok(). A payload that did not fit is never published half-written.
 *
 * jw_raw() splices a byte span in verbatim. It is how a value whose JSON type
 * is not ours to choose -- a correlation tag echoed back from an inbound
 * command -- is returned exactly as it arrived, rather than being re-encoded
 * into something that no longer compares equal.
 */
#ifndef TSB_CORE_UTIL_JSONW_H
#define TSB_CORE_UTIL_JSONW_H

#include "core/util/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JW_MAX_DEPTH 8

typedef struct {
    strbuf_t *sb;
    int       depth;
    bool      first[JW_MAX_DEPTH + 1];
    bool      too_deep;
} jsonw_t;

void jw_init(jsonw_t *w, strbuf_t *sb);

/** False if anything was dropped, or nesting went past JW_MAX_DEPTH. */
bool jw_ok(const jsonw_t *w);

void jw_obj_open(jsonw_t *w);
void jw_obj_close(jsonw_t *w);
void jw_arr_open(jsonw_t *w);
void jw_arr_close(jsonw_t *w);

/** Write a key. The value must follow immediately. */
void jw_key(jsonw_t *w, const char *key);

/* Values, for use inside an array or after jw_key(). */
void jw_str(jsonw_t *w, const char *s);
void jw_uint(jsonw_t *w, uint64_t v);
void jw_int(jsonw_t *w, int64_t v);
void jw_bool(jsonw_t *w, bool v);
void jw_null(jsonw_t *w);
void jw_raw(jsonw_t *w, const char *p, size_t n);

/** A byte span as a lower-case hex string. */
void jw_hex(jsonw_t *w, const uint8_t *p, size_t n);

/* Key plus value, the common case. */
void jw_kstr(jsonw_t *w, const char *key, const char *s);
void jw_kuint(jsonw_t *w, const char *key, uint64_t v);
void jw_kint(jsonw_t *w, const char *key, int64_t v);
void jw_kbool(jsonw_t *w, const char *key, bool v);
void jw_knull(jsonw_t *w, const char *key);
void jw_kraw(jsonw_t *w, const char *key, const char *p, size_t n);
void jw_khex(jsonw_t *w, const char *key, const uint8_t *p, size_t n);

/** A key whose name is a decimal number, as service-keyed objects need. */
void jw_key_uint(jsonw_t *w, uint64_t key);

#endif /* TSB_CORE_UTIL_JSONW_H */
