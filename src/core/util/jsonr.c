/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/util/jsonr.h"

#include <string.h>

typedef struct {
    const char *js;
    size_t      len;
    size_t      pos;
    jr_tok_t   *toks;
    int         max;
    int         next;
    int         super;     /* the container currently being filled */
    int         depth;
} jr_parser_t;

static jr_tok_t *alloc_tok(jr_parser_t *p)
{
    jr_tok_t *t;

    if (p->next >= p->max)
        return NULL;

    t = &p->toks[p->next++];
    t->type   = JR_UNDEF;
    t->start  = -1;
    t->end    = -1;
    t->size   = 0;
    t->parent = -1;

    return t;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Strings: the token spans the contents, not the quotes. Escapes are only
 * validated here; decoding happens in jr_str, on demand. */
static result_t parse_string(jr_parser_t *p)
{
    size_t    start = p->pos + 1u;
    size_t    i;
    jr_tok_t *t;

    for (i = start; i < p->len; i++) {
        char c = p->js[i];

        if (c == '"') {
            t = alloc_tok(p);
            if (!t)
                return RES_ETOOBIG;
            t->type   = JR_STRING;
            t->start  = (int)start;
            t->end    = (int)i;
            t->parent = p->super;
            p->pos    = i;
            return RES_OK;
        }

        if (c == '\\') {
            int k;

            if (i + 1u >= p->len)
                return RES_EFORMAT;
            i++;
            switch (p->js[i]) {
            case '"': case '\\': case '/': case 'b':
            case 'f': case 'r':  case 'n': case 't':
                break;
            case 'u':
                if (i + 4u >= p->len)
                    return RES_EFORMAT;
                for (k = 1; k <= 4; k++) {
                    if (hexval(p->js[i + (size_t)k]) < 0)
                        return RES_EFORMAT;
                }
                i += 4u;
                break;
            default:
                return RES_EFORMAT;
            }
            continue;
        }

        /* Unescaped control characters are not allowed in a JSON string. */
        if ((unsigned char)c < 0x20u)
            return RES_EFORMAT;
    }
    return RES_EFORMAT;     /* unterminated */
}

static bool lit_eq(const char *s, size_t n, const char *lit)
{
    return strlen(lit) == n && strncmp(s, lit, n) == 0;
}

/*
 * A JSON number: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
 *
 * Worth spelling out rather than accepting any run of non-delimiters. "tru",
 * "01" and "1.2.3" all look like values to a lax scanner, and a control
 * command carrying one of them should be refused, not half-understood.
 */
static bool is_number(const char *s, size_t n)
{
    size_t i = 0;

    if (n == 0)
        return false;

    if (s[i] == '-')
        i++;

    if (i >= n)
        return false;

    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9')
            i++;
    } else {
        return false;
    }

    if (i < n && s[i] == '.') {
        size_t digits = 0;

        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            digits++;
        }
        if (digits == 0)
            return false;
    }

    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t digits = 0;

        i++;
        if (i < n && (s[i] == '+' || s[i] == '-'))
            i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            digits++;
        }
        if (digits == 0)
            return false;
    }

    return i == n;
}

static result_t parse_primitive(jr_parser_t *p)
{
    size_t    start = p->pos;
    size_t    i;
    size_t    n;
    jr_tok_t *t;

    for (i = start; i < p->len; i++) {
        char c = p->js[i];

        if (c == ',' || c == '}' || c == ']' ||
            c == ' ' || c == '\t' || c == '\n' || c == '\r')
            break;
        if ((unsigned char)c < 0x20u || (unsigned char)c >= 0x7Fu)
            return RES_EFORMAT;
    }
    n = i - start;
    if (n == 0)
        return RES_EFORMAT;

    if (!lit_eq(p->js + start, n, "true") &&
        !lit_eq(p->js + start, n, "false") &&
        !lit_eq(p->js + start, n, "null") &&
        !is_number(p->js + start, n))
        return RES_EFORMAT;

    t = alloc_tok(p);
    if (!t)
        return RES_ETOOBIG;
    t->type   = JR_PRIMITIVE;
    t->start  = (int)start;
    t->end    = (int)i;
    t->parent = p->super;
    p->pos    = i - 1u;      /* the loop below advances past it */

    return RES_OK;
}

/*
 * What may legally appear next.
 *
 * Tracking this is what separates a parser from a scanner. Without it
 * {"a":}, {"a" 1} and a trailing comma all pass, because each is a plausible
 * sequence of tokens -- and a control command that parses into something other
 * than what was sent is worse than one that is refused.
 */
typedef enum {
    EX_VALUE = 0,     /* a value must follow                            */
    EX_FIRST_VALUE,   /* a value, or the empty array's closer           */
    EX_KEY,           /* a key must follow                              */
    EX_FIRST_KEY,     /* a key, or the empty object's closer            */
    EX_COLON,
    EX_COMMA,         /* a comma, or this container's closer            */
    EX_DONE           /* the root value is complete; only spaces remain */
} jr_expect_t;

result_t jr_parse(const char *js, size_t len, jr_tok_t *toks, int max,
                  int *out_count)
{
    jr_parser_t p;
    result_t    r;
    jr_expect_t exp = EX_VALUE;
    jr_type_t   ctype[JR_MAX_DEPTH];

    if (!js || !toks || !out_count || max <= 0)
        return RES_EINVAL;

    p.js    = js;
    p.len   = len;
    p.pos   = 0;
    p.toks  = toks;
    p.max   = max;
    p.next  = 0;
    p.super = -1;
    p.depth = 0;

    for (; p.pos < len; p.pos++) {
        char      c = js[p.pos];
        jr_tok_t *t;

        switch (c) {
        case '{':
        case '[':
            if (exp != EX_VALUE && exp != EX_FIRST_VALUE)
                return RES_EFORMAT;
            if (p.depth >= JR_MAX_DEPTH)
                return RES_EFORMAT;

            t = alloc_tok(&p);
            if (!t)
                return RES_ETOOBIG;
            t->type  = (c == '{') ? JR_OBJECT : JR_ARRAY;
            t->start = (int)p.pos;
            if (p.super != -1) {
                toks[p.super].size++;
                t->parent = p.super;
            }
            ctype[p.depth++] = t->type;
            p.super = p.next - 1;
            exp = (c == '{') ? EX_FIRST_KEY : EX_FIRST_VALUE;
            break;

        case '}':
        case ']': {
            jr_type_t want = (c == '}') ? JR_OBJECT : JR_ARRAY;
            int       i;

            if (p.depth == 0 || ctype[p.depth - 1] != want)
                return RES_EFORMAT;
            /* An empty container, or one whose last value is complete. A
             * trailing comma leaves EX_KEY or EX_VALUE and is refused. */
            if (exp != EX_COMMA &&
                !(want == JR_OBJECT && exp == EX_FIRST_KEY) &&
                !(want == JR_ARRAY  && exp == EX_FIRST_VALUE))
                return RES_EFORMAT;

            p.depth--;

            for (i = p.next - 1; i >= 0; i--) {
                if (toks[i].start != -1 && toks[i].end == -1) {
                    if (toks[i].type != want)
                        return RES_EFORMAT;
                    toks[i].end = (int)p.pos + 1;
                    p.super     = toks[i].parent;
                    break;
                }
            }
            if (i < 0)
                return RES_EFORMAT;

            exp = (p.depth == 0) ? EX_DONE : EX_COMMA;
            break;
        }

        case '"':
            if (exp == EX_KEY || exp == EX_FIRST_KEY) {
                r = parse_string(&p);
                if (r != RES_OK)
                    return r;
                if (p.super != -1)
                    toks[p.super].size++;
                exp = EX_COLON;
            } else if (exp == EX_VALUE || exp == EX_FIRST_VALUE) {
                r = parse_string(&p);
                if (r != RES_OK)
                    return r;
                if (p.super != -1)
                    toks[p.super].size++;
                exp = (p.depth == 0) ? EX_DONE : EX_COMMA;
            } else {
                return RES_EFORMAT;
            }
            break;

        case ':':
            if (exp != EX_COLON)
                return RES_EFORMAT;
            /* The key just written owns the value that follows. */
            p.super = p.next - 1;
            exp = EX_VALUE;
            break;

        case ',':
            if (exp != EX_COMMA || p.depth == 0)
                return RES_EFORMAT;
            if (p.super != -1 &&
                toks[p.super].type != JR_ARRAY &&
                toks[p.super].type != JR_OBJECT)
                p.super = toks[p.super].parent;
            exp = (ctype[p.depth - 1] == JR_OBJECT) ? EX_KEY : EX_VALUE;
            break;

        case ' ': case '\t': case '\n': case '\r':
            break;

        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 't': case 'f': case 'n':
            if (exp != EX_VALUE && exp != EX_FIRST_VALUE)
                return RES_EFORMAT;
            r = parse_primitive(&p);
            if (r != RES_OK)
                return r;
            if (p.super != -1)
                toks[p.super].size++;
            exp = (p.depth == 0) ? EX_DONE : EX_COMMA;
            break;

        default:
            return RES_EFORMAT;
        }
    }

    /* Anything short of a complete root value -- an unclosed container, a
     * dangling key, an empty document -- lands here. */
    if (exp != EX_DONE || p.next == 0)
        return RES_EFORMAT;

    *out_count = p.next;
    return RES_OK;
}

int jr_skip(const jr_tok_t *toks, int count, int i)
{
    int end;

    if (!toks || i < 0 || i >= count)
        return count;

    end = toks[i].end;
    i++;
    while (i < count && toks[i].start < end)
        i++;

    return i;
}

bool jr_streq(const char *js, const jr_tok_t *t, const char *s)
{
    size_t n;

    if (!js || !t || !s || t->type != JR_STRING)
        return false;
    if (t->end < t->start)
        return false;

    n = (size_t)(t->end - t->start);

    return strlen(s) == n && strncmp(js + t->start, s, n) == 0;
}

int jr_obj_iter(const jr_tok_t *toks, int count, int obj, int *cursor)
{
    int i;

    if (!toks || !cursor || obj < 0 || obj >= count)
        return -1;
    if (toks[obj].type != JR_OBJECT)
        return -1;

    if (*cursor == 0)
        *cursor = obj + 1;

    i = *cursor;
    if (i >= count || toks[i].start >= toks[obj].end)
        return -1;

    /* Advance past the key and the whole of its value. */
    *cursor = jr_skip(toks, count, i + 1);

    return i;
}

int jr_obj_get(const char *js, const jr_tok_t *toks, int count, int obj,
               const char *key)
{
    int cursor = 0;
    int k;

    while ((k = jr_obj_iter(toks, count, obj, &cursor)) >= 0) {
        if (k + 1 < count && jr_streq(js, &toks[k], key))
            return k + 1;
    }
    return -1;
}

result_t jr_int(const char *js, const jr_tok_t *t, int64_t *out)
{
    int      i;
    bool     neg = false;
    uint64_t mag = 0;
    bool     any = false;

    if (!js || !t || !out)
        return RES_EINVAL;
    if (t->type != JR_PRIMITIVE || t->end <= t->start)
        return RES_EFORMAT;

    i = t->start;
    if (js[i] == '-') {
        neg = true;
        i++;
    }

    for (; i < t->end; i++) {
        char c = js[i];

        if (c < '0' || c > '9')
            return RES_EFORMAT;      /* fractions and exponents are not ours */
        if (mag > (uint64_t)0x7FFFFFFFFFFFFFFF / 10u)
            return RES_ETOOBIG;
        mag = mag * 10u + (uint64_t)(c - '0');
        if (mag > (uint64_t)0x7FFFFFFFFFFFFFFF)
            return RES_ETOOBIG;
        any = true;
    }
    if (!any)
        return RES_EFORMAT;

    *out = neg ? -(int64_t)mag : (int64_t)mag;
    return RES_OK;
}

result_t jr_uint16(const char *js, const jr_tok_t *t, uint16_t *out)
{
    int64_t  v = 0;
    result_t r;

    if (!out)
        return RES_EINVAL;

    r = jr_int(js, t, &v);
    if (r != RES_OK)
        return r;
    if (v < 0 || v > 0xFFFF)
        return RES_ETOOBIG;

    *out = (uint16_t)v;
    return RES_OK;
}

result_t jr_bool(const char *js, const jr_tok_t *t, bool *out)
{
    if (!js || !t || !out)
        return RES_EINVAL;
    if (t->type != JR_PRIMITIVE)
        return RES_EFORMAT;

    if ((size_t)(t->end - t->start) == 4u &&
        strncmp(js + t->start, "true", 4) == 0) {
        *out = true;
        return RES_OK;
    }
    if ((size_t)(t->end - t->start) == 5u &&
        strncmp(js + t->start, "false", 5) == 0) {
        *out = false;
        return RES_OK;
    }
    return RES_EFORMAT;
}

/* Encode one code point as UTF-8. Returns the number of bytes, or 0. */
static size_t utf8_put(char *out, size_t cap, uint32_t cp)
{
    if (cp < 0x80u) {
        if (cap < 1u) return 0;
        out[0] = (char)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        if (cap < 2u) return 0;
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        if (cap < 3u) return 0;
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    if (cap < 4u) return 0;
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4u;
}

static uint32_t hex4(const char *p)
{
    return (uint32_t)((hexval(p[0]) << 12) | (hexval(p[1]) << 8) |
                      (hexval(p[2]) << 4)  |  hexval(p[3]));
}

result_t jr_str(const char *js, const jr_tok_t *t, char *out, size_t cap)
{
    int    i;
    size_t o = 0;

    if (!js || !t || !out || cap == 0)
        return RES_EINVAL;
    if (t->type != JR_STRING)
        return RES_EFORMAT;

    for (i = t->start; i < t->end; i++) {
        char c = js[i];

        if (c != '\\') {
            if (o + 1u >= cap)
                return RES_ETOOBIG;
            out[o++] = c;
            continue;
        }

        i++;
        if (i >= t->end)
            return RES_EFORMAT;

        switch (js[i]) {
        case '"':  c = '"';  break;
        case '\\': c = '\\'; break;
        case '/':  c = '/';  break;
        case 'b':  c = '\b'; break;
        case 'f':  c = '\f'; break;
        case 'n':  c = '\n'; break;
        case 'r':  c = '\r'; break;
        case 't':  c = '\t'; break;
        case 'u': {
            uint32_t cp;
            size_t   n;

            /* Four hex digits must sit at i+1 .. i+4, inside the token. */
            if (i + 4 >= t->end)
                return RES_EFORMAT;
            cp = hex4(js + i + 1);
            i += 4;

            if (cp >= 0xD800u && cp <= 0xDBFFu) {
                uint32_t lo;

                /* A high surrogate must be followed by its low half. */
                /* The low half occupies i+1 .. i+6: a backslash, a u, and
                 * four hex digits, all inside the token. */
                if (i + 6 >= t->end || js[i + 1] != '\\' ||
                    js[i + 2] != 'u')
                    return RES_EFORMAT;
                lo = hex4(js + i + 3);
                if (lo < 0xDC00u || lo > 0xDFFFu)
                    return RES_EFORMAT;
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                i += 6;
            } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                return RES_EFORMAT;      /* a low surrogate on its own */
            }

            n = utf8_put(out + o, cap - o - 1u, cp);
            if (n == 0)
                return RES_ETOOBIG;
            o += n;
            continue;
        }
        default:
            return RES_EFORMAT;
        }

        if (o + 1u >= cap)
            return RES_ETOOBIG;
        out[o++] = c;
    }

    out[o] = '\0';
    return RES_OK;
}

void jr_raw(const char *js, const jr_tok_t *t, const char **p, size_t *n)
{
    if (!p || !n)
        return;
    if (!js || !t || t->end < t->start) {
        *p = NULL;
        *n = 0;
        return;
    }

    /* A string token spans its contents; the quotes are part of the value. */
    if (t->type == JR_STRING) {
        *p = js + t->start - 1;
        *n = (size_t)(t->end - t->start) + 2u;
        return;
    }

    *p = js + t->start;
    *n = (size_t)(t->end - t->start);
}
