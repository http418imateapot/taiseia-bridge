/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/util/strbuf.h"

#include <string.h>

void sb_init(strbuf_t *sb, char *buf, size_t cap)
{
    if (!sb)
        return;

    sb->buf      = buf;
    sb->cap      = cap;
    sb->len      = 0;
    sb->overflow = (buf == NULL || cap == 0);

    if (buf && cap > 0)
        buf[0] = '\0';
}

void sb_reset(strbuf_t *sb)
{
    if (!sb)
        return;

    sb->len      = 0;
    sb->overflow = (sb->buf == NULL || sb->cap == 0);

    if (sb->buf && sb->cap > 0)
        sb->buf[0] = '\0';
}

bool sb_ok(const strbuf_t *sb)
{
    return sb && !sb->overflow;
}

size_t sb_len(const strbuf_t *sb)
{
    return sb ? sb->len : 0;
}

const char *sb_str(const strbuf_t *sb)
{
    return (sb && sb->buf) ? sb->buf : "";
}

void sb_write(strbuf_t *sb, const char *p, size_t n)
{
    if (!sb || sb->overflow)
        return;
    if (!p) {
        sb->overflow = true;
        return;
    }

    /* One byte is always reserved for the terminator. Written as a
     * subtraction on the capacity rather than an addition on n, so a huge n
     * cannot wrap the comparison round. */
    if (n >= sb->cap - sb->len) {
        sb->overflow = true;
        return;
    }

    memcpy(sb->buf + sb->len, p, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_putc(strbuf_t *sb, char c)
{
    sb_write(sb, &c, 1);
}

void sb_puts(strbuf_t *sb, const char *s)
{
    if (!s) {
        if (sb)
            sb->overflow = true;
        return;
    }
    sb_write(sb, s, strlen(s));
}

void sb_putu(strbuf_t *sb, uint64_t v)
{
    char   tmp[20];        /* 2^64-1 is twenty digits */
    size_t n = 0;

    if (v == 0) {
        sb_putc(sb, '0');
        return;
    }

    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    while (n > 0)
        sb_putc(sb, tmp[--n]);
}

void sb_puti(strbuf_t *sb, int64_t v)
{
    if (v < 0) {
        sb_putc(sb, '-');
        /* Negating INT64_MIN is undefined, so widen before negating. */
        sb_putu(sb, (uint64_t)(-(v + 1)) + 1u);
        return;
    }
    sb_putu(sb, (uint64_t)v);
}

void sb_puthex(strbuf_t *sb, const uint8_t *p, size_t n)
{
    static const char k_hex[] = "0123456789abcdef";
    size_t i;

    if (!p) {
        if (sb && n > 0)
            sb->overflow = true;
        return;
    }

    for (i = 0; i < n; i++) {
        sb_putc(sb, k_hex[(p[i] >> 4) & 0x0Fu]);
        sb_putc(sb, k_hex[p[i] & 0x0Fu]);
    }
}
