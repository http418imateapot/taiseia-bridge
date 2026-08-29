/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/util/result.h"

const char *res_str(result_t r)
{
    switch (r) {
    case RES_OK:           return "ok";
    case RES_EINVAL:       return "invalid argument";
    case RES_EFORMAT:      return "malformed input";
    case RES_ETOOBIG:      return "does not fit";
    case RES_EIO:          return "transport failure";
    case RES_ETIMEOUT:     return "timed out";
    case RES_EAGAIN:       return "not ready";
    case RES_ENOTFOUND:    return "not found";
    case RES_EUNSUPPORTED: return "unsupported";
    case RES_EBUSY:        return "busy";
    }
    return "unknown";
}
