/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A bounded string builder over caller-supplied memory.
 *
 * OVERFLOW IS STICKY. Once an append would not fit, the buffer is marked and
 * every later append is dropped -- including ones that would have fit. The
 * alternative is a buffer holding the first n bytes of a message, which for
 * JSON means syntactically valid-looking output that is silently truncated.
 * A caller checks sb_ok() once, at the end, instead of after every append.
 *
 * The buffer is always NUL-terminated, overflow included, so sb_str() is
 * always safe to pass to something expecting a C string.
 *
 * There is no snprintf here: <stdio.h> is not available in the core, and the
 * number formatting below is what replaces it.
 */
#ifndef TSB_CORE_UTIL_STRBUF_H
#define TSB_CORE_UTIL_STRBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char  *buf;
    size_t cap;      /* including the terminator */
    size_t len;
    bool   overflow;
} strbuf_t;

/** Bind to `cap` bytes at `buf`. A capacity of zero is accepted and useless. */
void sb_init(strbuf_t *sb, char *buf, size_t cap);

/** Empty it, and clear the overflow mark. */
void sb_reset(strbuf_t *sb);

/** False once anything has been dropped. */
bool sb_ok(const strbuf_t *sb);

size_t      sb_len(const strbuf_t *sb);
const char *sb_str(const strbuf_t *sb);

void sb_putc(strbuf_t *sb, char c);
void sb_puts(strbuf_t *sb, const char *s);
void sb_write(strbuf_t *sb, const char *p, size_t n);

/** Decimal, no padding. */
void sb_putu(strbuf_t *sb, uint64_t v);
void sb_puti(strbuf_t *sb, int64_t v);

/** Lower-case hex, two characters per byte. */
void sb_puthex(strbuf_t *sb, const uint8_t *p, size_t n);

#endif /* TSB_CORE_UTIL_STRBUF_H */
