/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/topic.h"

#include "core/util/strbuf.h"

#include <string.h>

result_t topic_render(char *out, size_t cap, const char *tmpl,
                      const char *prefix, const char *device_id)
{
    strbuf_t sb;
    size_t   i;

    if (!out || !tmpl || !prefix || !device_id || cap == 0)
        return RES_EINVAL;

    sb_init(&sb, out, cap);

    for (i = 0; tmpl[i]; i++) {
        if (tmpl[i] != '{') {
            sb_putc(&sb, tmpl[i]);
            continue;
        }

        if (strncmp(tmpl + i, "{prefix}", 8) == 0) {
            sb_puts(&sb, prefix);
            i += 7;
        } else if (strncmp(tmpl + i, "{id}", 4) == 0) {
            sb_puts(&sb, device_id);
            i += 3;
        } else {
            /* An unknown placeholder would publish to a topic nobody
             * subscribes to, and nothing would ever report it. */
            return RES_EFORMAT;
        }
    }

    if (!sb_ok(&sb))
        return RES_ETOOBIG;
    if (sb_len(&sb) == 0)
        return RES_EFORMAT;

    return RES_OK;
}

result_t topic_check_set(const char *const *tmpls, size_t n,
                         const char *prefix, const char *device_id,
                         size_t *which)
{
    char     rendered[8][TOPIC_MAX];
    size_t   i, j;
    result_t r;

    if (!tmpls || !which)
        return RES_EINVAL;
    if (n == 0 || n > sizeof(rendered) / sizeof(rendered[0]))
        return RES_ETOOBIG;

    *which = 0;

    for (i = 0; i < n; i++) {
        r = topic_render(rendered[i], TOPIC_MAX, tmpls[i], prefix, device_id);
        if (r != RES_OK) {
            *which = i;
            return r;
        }
    }

    for (i = 0; i < n; i++) {
        for (j = 0; j < i; j++) {
            if (strcmp(rendered[i], rendered[j]) == 0) {
                *which = i;
                return RES_EINVAL;
            }
        }
    }

    return RES_OK;
}
