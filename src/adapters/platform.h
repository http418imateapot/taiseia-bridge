/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The platform seam.
 *
 * One header, two implementations, no `#ifdef` above this line. An application
 * includes this and never learns which operating system it is on; everything
 * below it is confined to src/adapters/posix and src/adapters/win32.
 *
 * Nothing here is reachable from src/core, which is freestanding and gets what
 * it needs through the ports instead.
 */
#ifndef TSB_ADAPTERS_PLATFORM_H
#define TSB_ADAPTERS_PLATFORM_H

#include "core/util/result.h"
#include "ports/socket.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Prepare the platform. Winsock needs starting; POSIX needs SIGPIPE ignored,
 * because the default disposition kills the process the first time a broker
 * disappears mid-write.
 */
result_t platform_init(void);
void     platform_shutdown(void);

/* --------------------------------------------------------------------- time */

/**
 * Milliseconds from an arbitrary origin, monotonic.
 *
 * Everything that schedules uses this, so a system clock stepped by NTP -- or
 * by a device booting with no clock at all and then acquiring one -- cannot
 * make a timer fire years early or never.
 */
int64_t platform_now_ms(void);

/**
 * Wall-clock UTC as "YYYY-MM-DDTHH:MM:SS.mmmZ". False if it does not fit.
 *
 * This is the one that goes into payloads, and it *is* the system clock: a
 * device that boots before its clock is set will publish timestamps that are
 * wrong, with nothing in the payload to say so.
 */
bool platform_iso8601_utc(char *out, size_t cap);

#define PLATFORM_TIMESTAMP_MAX 32u

void platform_sleep_ms(int ms);

/* ------------------------------------------------------------------ logging */

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

void        log_set_level(log_level_t level);
log_level_t log_level_from_str(const char *s);   /* LOG_INFO when unknown */
bool        log_level_valid(const char *s);

void log_emit(log_level_t level, const char *fmt, ...);

#define LOG_E(...) log_emit(LOG_ERROR, __VA_ARGS__)
#define LOG_W(...) log_emit(LOG_WARN,  __VA_ARGS__)
#define LOG_I(...) log_emit(LOG_INFO,  __VA_ARGS__)
#define LOG_D(...) log_emit(LOG_DEBUG, __VA_ARGS__)

/* ------------------------------------------------------------------ sockets */

typedef struct {
    /* A file descriptor on POSIX, a SOCKET on Windows. Negative means closed;
     * keeping it here rather than behind a void* is what lets a caller hold
     * one without allocating. */
    intptr_t handle;
    bool     connecting;
} platform_socket_t;

/** Bind `out` to `s`, which the caller owns and keeps alive. */
void platform_socket_bind(platform_socket_t *s, socket_t *out);

#endif /* TSB_ADAPTERS_PLATFORM_H */
