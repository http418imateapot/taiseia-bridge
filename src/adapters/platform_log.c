/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Logging, shared by both platforms.
 *
 * Lines go to stderr so that a service manager -- procd, systemd, the Windows
 * console -- captures them without this program knowing which one it is
 * talking to.
 */
#include "adapters/platform.h"

#include <stdio.h>
#include <string.h>

static log_level_t g_level = LOG_INFO;

static const char *const k_names[] = { "error", "warn", "info", "debug" };

void log_set_level(log_level_t level)
{
    if (level < LOG_ERROR)
        level = LOG_ERROR;
    if (level > LOG_DEBUG)
        level = LOG_DEBUG;

    g_level = level;
}

log_level_t log_level_from_str(const char *s)
{
    size_t i;

    if (s) {
        for (i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++) {
            if (strcmp(s, k_names[i]) == 0)
                return (log_level_t)i;
        }
    }
    return LOG_INFO;
}

bool log_level_valid(const char *s)
{
    size_t i;

    if (!s)
        return false;

    for (i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++) {
        if (strcmp(s, k_names[i]) == 0)
            return true;
    }
    return false;
}

void log_emit(log_level_t level, const char *fmt, ...)
{
    char    stamp[PLATFORM_TIMESTAMP_MAX];
    va_list ap;

    if (level > g_level || !fmt)
        return;

    if (!platform_iso8601_utc(stamp, sizeof(stamp)))
        stamp[0] = '\0';

    fprintf(stderr, "%s %-5s ", stamp, k_names[level]);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
    fflush(stderr);
}
