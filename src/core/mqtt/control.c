/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/control.h"

#include "core/taiseia/limits.h"
#include "core/util/jsonr.h"

#include <string.h>

/* An attribute key is the service identifier written in decimal. Nothing else
 * is accepted -- not "03", not " 3", not "0x03". A key we half-understand
 * would address a service the sender did not name.
 *
 * The range is the multi-byte format's, 0..65535, whatever the appliance's
 * format: whether the appliance has that service is the capability check's
 * question, and it answers `unknown`. A key above 65535 names no service in
 * either format and refuses the command. */
static bool key_to_service(const char *js, const jr_tok_t *t, uint16_t *out)
{
    int      i;
    uint32_t v = 0;
    int      digits = 0;

    if (t->type != JR_STRING)
        return false;

    for (i = t->start; i < t->end; i++) {
        char c = js[i];

        if (c < '0' || c > '9')
            return false;
        v = v * 10u + (uint32_t)(c - '0');
        digits++;
        if (v > TS_MB_SVC_ID_MAX)
            return false;
    }

    if (digits == 0 || digits > 5)
        return false;
    if (digits > 1 && js[t->start] == '0')
        return false;                       /* no leading zeros */

    *out = (uint16_t)v;
    return true;
}

result_t control_parse(const char *payload, size_t len, control_cmd_t *out)
{
    jr_tok_t    toks[CONTROL_MAX_TOKENS];
    int         count = 0;
    int         attrs;
    int         cursor = 0;
    int         k;
    result_t    r;

    if (!payload || !out)
        return RES_EINVAL;

    memset(out, 0, sizeof(*out));

    r = jr_parse(payload, len, toks, CONTROL_MAX_TOKENS, &count);
    if (r != RES_OK)
        return r;
    if (toks[0].type != JR_OBJECT)
        return RES_EFORMAT;

    /* The tag is optional and its JSON type is not ours to choose. */
    {
        int tag = jr_obj_get(payload, toks, count, 0, "tag");

        if (tag > 0) {
            const char *p = NULL;
            size_t      n = 0;

            jr_raw(payload, &toks[tag], &p, &n);
            if (n > sizeof(out->tag))
                return RES_ETOOBIG;
            if (p && n > 0) {
                memcpy(out->tag, p, n);
                out->tag_len = n;
                out->has_tag = true;
            }
        }
    }

    attrs = jr_obj_get(payload, toks, count, 0, "attributes");
    if (attrs < 0 || toks[attrs].type != JR_OBJECT)
        return RES_EFORMAT;

    while ((k = jr_obj_iter(toks, count, attrs, &cursor)) >= 0) {
        uint16_t service = 0;
        uint16_t value = 0;

        if (k + 1 >= count)
            return RES_EFORMAT;
        if (!key_to_service(payload, &toks[k], &service))
            return RES_EFORMAT;
        if (jr_uint16(payload, &toks[k + 1], &value) != RES_OK)
            return RES_EFORMAT;

        if (out->n >= APPL_WRITE_BATCH_MAX)
            return RES_ETOOBIG;

        out->items[out->n].service_id = service;
        out->items[out->n].value      = value;
        out->items[out->n].result     = APPL_WRITE_OK;
        out->items[out->n].done       = false;
        out->n++;
    }

    /* A command that asks for nothing is not a command. Accepting it would
     * produce an acknowledgement with an empty result set, which reads as
     * success. */
    if (out->n == 0)
        return RES_EFORMAT;

    return RES_OK;
}
