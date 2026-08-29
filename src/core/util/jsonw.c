/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/util/jsonw.h"

void jw_init(jsonw_t *w, strbuf_t *sb)
{
    int i;

    if (!w)
        return;

    w->sb       = sb;
    w->depth    = 0;
    w->too_deep = false;
    for (i = 0; i <= JW_MAX_DEPTH; i++)
        w->first[i] = true;
}

bool jw_ok(const jsonw_t *w)
{
    return w && !w->too_deep && sb_ok(w->sb);
}

/* A separator before every value except the first at this level. */
static void separate(jsonw_t *w)
{
    if (!w || w->too_deep)
        return;

    if (w->depth >= 0 && w->depth <= JW_MAX_DEPTH) {
        if (!w->first[w->depth])
            sb_putc(w->sb, ',');
        w->first[w->depth] = false;
    }
}

static void push(jsonw_t *w)
{
    if (w->depth >= JW_MAX_DEPTH) {
        w->too_deep = true;
        return;
    }
    w->depth++;
    w->first[w->depth] = true;
}

static void pop(jsonw_t *w)
{
    if (w->depth <= 0) {
        w->too_deep = true;
        return;
    }
    w->depth--;
}

/*
 * Escape per RFC 8259: the two mandatory characters, the five with short
 * forms, and anything below 0x20 as \u00XX. Bytes at or above 0x80 pass
 * through -- the input is expected to be UTF-8 already, and re-encoding it
 * here would corrupt anything multi-byte.
 */
static void put_escaped(jsonw_t *w, const char *s)
{
    static const char k_hex[] = "0123456789abcdef";

    if (!s) {
        sb_puts(w->sb, NULL);      /* marks overflow */
        return;
    }

    sb_putc(w->sb, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        switch (c) {
        case '"':  sb_puts(w->sb, "\\\""); continue;
        case '\\': sb_puts(w->sb, "\\\\"); continue;
        case '\n': sb_puts(w->sb, "\\n");  continue;
        case '\r': sb_puts(w->sb, "\\r");  continue;
        case '\t': sb_puts(w->sb, "\\t");  continue;
        case '\b': sb_puts(w->sb, "\\b");  continue;
        case '\f': sb_puts(w->sb, "\\f");  continue;
        default:   break;
        }

        if (c < 0x20u) {
            sb_puts(w->sb, "\\u00");
            sb_putc(w->sb, k_hex[(c >> 4) & 0x0Fu]);
            sb_putc(w->sb, k_hex[c & 0x0Fu]);
            continue;
        }

        sb_putc(w->sb, (char)c);
    }
    sb_putc(w->sb, '"');
}

void jw_obj_open(jsonw_t *w)
{
    if (!w) return;
    separate(w);
    sb_putc(w->sb, '{');
    push(w);
}

void jw_obj_close(jsonw_t *w)
{
    if (!w) return;
    pop(w);
    sb_putc(w->sb, '}');
}

void jw_arr_open(jsonw_t *w)
{
    if (!w) return;
    separate(w);
    sb_putc(w->sb, '[');
    push(w);
}

void jw_arr_close(jsonw_t *w)
{
    if (!w) return;
    pop(w);
    sb_putc(w->sb, ']');
}

void jw_key(jsonw_t *w, const char *key)
{
    if (!w) return;
    separate(w);
    put_escaped(w, key);
    sb_putc(w->sb, ':');
    /* The value that follows must not emit another separator. */
    if (w->depth >= 0 && w->depth <= JW_MAX_DEPTH)
        w->first[w->depth] = true;
}

void jw_key_uint(jsonw_t *w, uint64_t key)
{
    if (!w) return;
    separate(w);
    sb_putc(w->sb, '"');
    sb_putu(w->sb, key);
    sb_puts(w->sb, "\":");
    if (w->depth >= 0 && w->depth <= JW_MAX_DEPTH)
        w->first[w->depth] = true;
}

void jw_str(jsonw_t *w, const char *s)
{
    if (!w) return;
    separate(w);
    put_escaped(w, s);
}

void jw_uint(jsonw_t *w, uint64_t v)
{
    if (!w) return;
    separate(w);
    sb_putu(w->sb, v);
}

void jw_int(jsonw_t *w, int64_t v)
{
    if (!w) return;
    separate(w);
    sb_puti(w->sb, v);
}

void jw_bool(jsonw_t *w, bool v)
{
    if (!w) return;
    separate(w);
    sb_puts(w->sb, v ? "true" : "false");
}

void jw_null(jsonw_t *w)
{
    if (!w) return;
    separate(w);
    sb_puts(w->sb, "null");
}

void jw_raw(jsonw_t *w, const char *p, size_t n)
{
    if (!w) return;
    separate(w);
    if (!p || n == 0) {
        sb_puts(w->sb, "null");
        return;
    }
    sb_write(w->sb, p, n);
}

void jw_kstr(jsonw_t *w, const char *key, const char *s)
{
    jw_key(w, key);
    jw_str(w, s);
}

void jw_kuint(jsonw_t *w, const char *key, uint64_t v)
{
    jw_key(w, key);
    jw_uint(w, v);
}

void jw_kint(jsonw_t *w, const char *key, int64_t v)
{
    jw_key(w, key);
    jw_int(w, v);
}

void jw_kbool(jsonw_t *w, const char *key, bool v)
{
    jw_key(w, key);
    jw_bool(w, v);
}

void jw_knull(jsonw_t *w, const char *key)
{
    jw_key(w, key);
    jw_null(w);
}

void jw_kraw(jsonw_t *w, const char *key, const char *p, size_t n)
{
    jw_key(w, key);
    jw_raw(w, p, n);
}
