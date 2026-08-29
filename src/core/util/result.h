/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Uniform result type.
 *
 * RULE: every fallible function returns a result_t. A function that can fail
 * must not return void and signal failure some other way -- silent failure is
 * how a protocol implementation ends up reporting stale values as fresh ones.
 */
#ifndef TSB_CORE_UTIL_RESULT_H
#define TSB_CORE_UTIL_RESULT_H

typedef enum {
    RES_OK = 0,
    RES_EINVAL,        /* bad argument -- a programming error                */
    RES_EFORMAT,       /* the peer sent something malformed                  */
    RES_ETOOBIG,       /* would not fit in the caller's buffer               */
    RES_EIO,           /* transport failed; the caller should reconnect      */
    RES_ETIMEOUT,      /* a deadline expired                                 */
    RES_EAGAIN,        /* would block, or nothing is ready yet               */
    RES_ENOTFOUND,     /* the requested item does not exist                  */
    RES_EUNSUPPORTED,  /* understood, but not supported here                 */
    RES_EBUSY          /* another operation is already in flight             */
} result_t;

const char *res_str(result_t r);

#endif /* TSB_CORE_UTIL_RESULT_H */
