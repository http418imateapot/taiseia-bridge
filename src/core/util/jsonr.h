/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A bounded JSON reader.
 *
 * Parses into a caller-supplied token array: no allocation, no recursion, and
 * a hard depth limit, so a hostile or merely enormous document cannot exhaust
 * memory or the stack. Tokens reference spans of the original buffer; nothing
 * is copied until the caller asks for it.
 *
 * jr_raw() hands back the exact bytes of a value. That is how a correlation
 * tag of unconstrained JSON type is echoed back to a sender byte for byte,
 * rather than being decoded and re-encoded into something that no longer
 * compares equal to what they sent.
 */
#ifndef TSB_CORE_UTIL_JSONR_H
#define TSB_CORE_UTIL_JSONR_H

#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JR_MAX_DEPTH   8
#define JR_MAX_TOKENS  256

typedef enum {
    JR_UNDEF = 0,
    JR_OBJECT,
    JR_ARRAY,
    JR_STRING,      /* span excludes the surrounding quotes */
    JR_PRIMITIVE    /* number, true, false, null             */
} jr_type_t;

typedef struct {
    jr_type_t type;
    int       start;   /* offset of the first byte of the value    */
    int       end;     /* one past the last byte                   */
    int       size;    /* object: members; array: elements;
                          object key: 1; other scalars: 0          */
    int       parent;  /* index of the parent token, -1 at the root */
} jr_tok_t;

/**
 * Parse `js[0..len)`.
 *
 * RES_EFORMAT when malformed or when nesting passes JR_MAX_DEPTH,
 * RES_ETOOBIG when the document needs more than `max` tokens.
 */
result_t jr_parse(const char *js, size_t len, jr_tok_t *toks, int max,
                  int *out_count);

/** Index one past the subtree rooted at `i` -- its next sibling's slot. */
int jr_skip(const jr_tok_t *toks, int count, int i);

/** Look up `key` in the object at `obj`. Returns the VALUE index, or -1. */
int jr_obj_get(const char *js, const jr_tok_t *toks, int count, int obj,
               const char *key);

/**
 * Walk an object's members. Returns the KEY index, or -1 when done; the value
 * is at the returned index plus one. `*cursor` starts at 0 and is advanced.
 */
int jr_obj_iter(const jr_tok_t *toks, int count, int obj, int *cursor);

bool jr_streq(const char *js, const jr_tok_t *t, const char *s);

/* Scalar accessors. Each rejects the wrong token type with RES_EFORMAT. */
result_t jr_int(const char *js, const jr_tok_t *t, int64_t *out);
result_t jr_uint16(const char *js, const jr_tok_t *t, uint16_t *out);
result_t jr_bool(const char *js, const jr_tok_t *t, bool *out);

/** Copy a string with escapes decoded. Always NUL-terminates on success. */
result_t jr_str(const char *js, const jr_tok_t *t, char *out, size_t cap);

/** The value's exact byte span, quotes and braces included. */
void jr_raw(const char *js, const jr_tok_t *t, const char **p, size_t *n);

#endif /* TSB_CORE_UTIL_JSONR_H */
