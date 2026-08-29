/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dispatch for the appliance link port.
 *
 * These wrappers exist so that a null link, or an adapter that leaves an
 * optional entry point unset, fails as a typed result at the call site rather
 * than as a null call through a vtable.
 */
#include "ports/appliance_link.h"

result_t link_submit(link_t *l, const link_req_t *req)
{
    if (!l || !l->vt || !l->vt->submit || !req)
        return RES_EINVAL;

    return l->vt->submit(l->self, req);
}

result_t link_poll(link_t *l, link_resp_t *out)
{
    if (!l || !l->vt || !l->vt->poll || !out)
        return RES_EINVAL;

    return l->vt->poll(l->self, out);
}

bool link_is_up(link_t *l)
{
    if (!l || !l->vt || !l->vt->is_up)
        return false;

    return l->vt->is_up(l->self);
}

void link_close(link_t *l)
{
    if (!l || !l->vt || !l->vt->close)
        return;

    l->vt->close(l->self);
    l->vt   = NULL;
    l->self = NULL;
}
