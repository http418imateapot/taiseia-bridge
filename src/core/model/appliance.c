/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/model/appliance.h"

#include <string.h>

void appl_policy_defaults(appl_policy_t *p)
{
    if (!p)
        return;

    p->sweep_interval_ms      = 60000;
    p->reregister_interval_ms = 300000;
    p->max_retries            = TS_MAX_RETRIES;
    p->max_misses             = 3;
    p->retry_backoff_min_ms   = 5000;
    p->retry_backoff_max_ms   = 60000;
}

void appl_init(appliance_t *a, const appl_policy_t *policy)
{
    if (!a)
        return;

    memset(a, 0, sizeof(*a));

    if (policy)
        a->policy = *policy;
    else
        appl_policy_defaults(&a->policy);

    if (a->policy.max_retries > TS_MAX_RETRIES)
        a->policy.max_retries = TS_MAX_RETRIES;
    if (a->policy.max_misses == 0)
        a->policy.max_misses = 1;

    a->state       = APPL_DOWN;
    a->backoff_ms  = a->policy.retry_backoff_min_ms;
    a->next_req_id = 1;
}

appl_state_t appl_state(const appliance_t *a)
{
    return a ? a->state : APPL_DOWN;
}

bool appl_online(const appliance_t *a)
{
    return a && (a->state == APPL_READY || a->state == APPL_READY_IDENTITY_ONLY);
}

unsigned appl_sweeps(const appliance_t *a)
{
    return a ? a->sweeps : 0;
}

const ts_registration_t *appl_identity(const appliance_t *a)
{
    if (!a || !a->reg_valid)
        return NULL;

    return &a->reg;
}

result_t appl_value(const appliance_t *a, uint8_t service_id, uint16_t *out)
{
    size_t i;

    if (!a || !out)
        return RES_EINVAL;

    service_id = (uint8_t)(service_id & TS_SVC_ID_MASK);

    for (i = 0; i < a->n_values; i++) {
        if (a->values[i].service_id != service_id)
            continue;
        if (!a->values[i].valid)
            return RES_ENOTFOUND;

        *out = a->values[i].value;
        return RES_OK;
    }
    return RES_ENOTFOUND;
}

const char *appl_state_str(appl_state_t s)
{
    switch (s) {
    case APPL_DOWN:                return "down";
    case APPL_IDENTIFYING:         return "identifying";
    case APPL_READY:               return "ready";
    case APPL_READY_IDENTITY_ONLY: return "ready (identity only)";
    }
    return "unknown";
}

const char *appl_write_result_str(appl_write_result_t r)
{
    switch (r) {
    case APPL_WRITE_OK:        return "written";
    case APPL_WRITE_UNKNOWN:   return "no such service";
    case APPL_WRITE_READ_ONLY: return "read only";
    case APPL_WRITE_NOT_TAKEN: return "not taken";
    case APPL_WRITE_NO_REPLY:  return "no reply";
    }
    return "unknown";
}

/* ------------------------------------------------------------- transitions */

static void go_down(appliance_t *a, int64_t now_ms)
{
    size_t i;

    a->state     = APPL_DOWN;
    a->reg_valid = false;
    a->in_flight = false;
    a->op        = APPL_OP_NONE;
    a->sweeping  = false;
    a->cursor    = 0;
    a->consecutive_failures = 0;

    /* Values from a link that is no longer answering are not values. Keeping
     * them would let a stale reading be published as though it were current. */
    for (i = 0; i < a->n_values; i++)
        a->values[i].valid = false;

    a->next_attempt_ms = now_ms + a->backoff_ms;

    a->backoff_ms *= 2;
    if (a->backoff_ms > a->policy.retry_backoff_max_ms)
        a->backoff_ms = a->policy.retry_backoff_max_ms;

    /* An unfinished batch cannot be finished. Say so rather than leaving the
     * caller waiting for a completion that will never arrive. */
    if (a->write.active) {
        size_t j;

        for (j = 0; j < a->write.n; j++) {
            if (!a->write.items[j].done) {
                a->write.items[j].done   = true;
                a->write.items[j].result = APPL_WRITE_NO_REPLY;
            }
        }
    }
}

static void adopt_registration(appliance_t *a, int64_t now_ms)
{
    size_t i;

    a->reg_valid  = true;
    a->wire_sa_id = (uint8_t)(a->reg.sa_id & 0xFFu);
    a->backoff_ms = a->policy.retry_backoff_min_ms;
    a->consecutive_failures = 0;

    /* The capability set may have changed, so every cached value is suspect. */
    memset(a->values, 0, sizeof(a->values));
    a->n_values = a->reg.n_caps;
    for (i = 0; i < a->n_values; i++)
        a->values[i].service_id = a->reg.caps[i].service_id;

    a->cursor        = 0;
    a->sweeping      = false;
    a->next_sweep_ms = now_ms;
    a->next_reregister_ms = now_ms + a->policy.reregister_interval_ms;

    /*
     * Polling uses the general request format. A multi-byte-format appliance
     * speaks a different request layout that is not implemented, so its
     * services cannot be read -- but its identity and capabilities are known
     * and worth publishing. Reporting an empty attribute set without saying
     * why would look like a device with nothing to report.
     */
    a->state = (a->reg.format == TS_FORMAT_GENERAL) ? APPL_READY
                                                    : APPL_READY_IDENTITY_ONLY;
}

/* ---------------------------------------------------------------- requests */

static void abandon(appliance_t *a, int64_t now_ms);

static result_t start(appliance_t *a, link_t *link, appl_op_t op,
                      uint8_t service_id, uint16_t value, int64_t now_ms)
{
    link_req_t req;
    result_t   r;

    memset(&req, 0, sizeof(req));
    req.id         = a->next_req_id++;
    req.sa_id      = a->wire_sa_id;
    req.service_id = service_id;
    req.value      = value;

    switch (op) {
    case APPL_OP_REGISTER: req.op = LINK_OP_REGISTER; break;
    case APPL_OP_READ:     req.op = LINK_OP_READ;     break;
    case APPL_OP_WRITE:    req.op = LINK_OP_WRITE;    break;
    case APPL_OP_NONE:
    default:
        return RES_EINVAL;
    }

    /* Recorded before submitting, so that a refusal can be routed through the
     * same failure handling as a transaction that was accepted and then went
     * wrong. */
    a->op         = op;
    a->req_id     = req.id;
    a->op_service = service_id;
    a->op_value   = value;

    r = link_submit(link, &req);
    if (r != RES_OK) {
        /* The adapter would not take it. Give up on this transaction rather
         * than retrying it on every tick for as long as the link stays
         * unwilling. */
        a->in_flight = false;
        a->attempt   = a->policy.max_retries + 1u;
        abandon(a, now_ms);
        return r;
    }

    a->in_flight         = true;
    a->stuck_deadline_ms = now_ms + APPL_STUCK_DEADLINE_MS;

    return RES_OK;
}

/* --------------------------------------------------------------- outcomes */

static void note_read_failure(appliance_t *a, uint8_t service_id)
{
    size_t i;

    for (i = 0; i < a->n_values; i++) {
        if (a->values[i].service_id != service_id)
            continue;

        a->values[i].misses++;
        if (a->values[i].misses >= a->policy.max_misses)
            a->values[i].valid = false;
        return;
    }
}

static void note_read_success(appliance_t *a, uint8_t service_id,
                              uint16_t value, int64_t now_ms)
{
    size_t i;

    for (i = 0; i < a->n_values; i++) {
        if (a->values[i].service_id != service_id)
            continue;

        a->values[i].value      = value;
        a->values[i].valid      = true;
        a->values[i].misses     = 0;
        a->values[i].updated_ms = now_ms;
        return;
    }
}

static void finish_write_item(appliance_t *a, appl_write_result_t result)
{
    if (a->write.cursor < a->write.n) {
        a->write.items[a->write.cursor].result = result;
        a->write.items[a->write.cursor].done   = true;
    }
    a->write.cursor++;
}

/* Advance past whatever the failed transaction was. */
static void abandon(appliance_t *a, int64_t now_ms)
{
    a->in_flight = false;
    a->attempt   = 0;
    a->consecutive_failures++;

    switch (a->op) {
    case APPL_OP_REGISTER:
        a->op = APPL_OP_NONE;
        go_down(a, now_ms);
        return;
    case APPL_OP_READ:
        note_read_failure(a, a->op_service);
        a->cursor++;
        break;
    case APPL_OP_WRITE:
        finish_write_item(a, APPL_WRITE_NO_REPLY);
        break;
    case APPL_OP_NONE:
    default:
        break;
    }

    a->op = APPL_OP_NONE;

    /* Nothing has answered for a while: treat the appliance as gone rather
     * than keep sweeping a link that is not there. */
    if (a->consecutive_failures >= a->policy.max_misses)
        go_down(a, now_ms);
}

static void handle_reply(appliance_t *a, link_t *link, const link_resp_t *resp,
                         int64_t now_ms)
{
    appl_op_t op = a->op;

    /* A reply to a request we are no longer waiting for. Discarding it is
     * right: acting on it would attribute one service's value to another. */
    if (resp->id != a->req_id)
        return;

    a->in_flight = false;

    if (resp->status != RES_OK) {
        a->attempt++;
        if (a->attempt <= a->policy.max_retries) {
            /* Retry the same transaction. The link, not us, decided it
             * failed; our part is only how many times to ask again. */
            a->in_flight = false;
            (void)start(a, link, op, a->op_service, a->op_value, now_ms);
            return;
        }
        abandon(a, now_ms);
        return;
    }

    a->attempt = 0;
    a->consecutive_failures = 0;
    a->op = APPL_OP_NONE;

    switch (op) {
    case APPL_OP_REGISTER: {
        result_t r = ts_registration_parse(resp->data, resp->data_len, &a->reg);

        if (r != RES_OK) {
            /* Well-formed frame, unusable contents. Retrying immediately would
             * just fetch the same bytes, so back off like any other failure. */
            go_down(a, now_ms);
            return;
        }
        adopt_registration(a, now_ms);
        return;
    }

    case APPL_OP_READ:
        if (resp->data_len >= 2u) {
            uint16_t v = (uint16_t)(((uint16_t)resp->data[0] << 8) |
                                    resp->data[1]);

            note_read_success(a, a->op_service, v, now_ms);
        } else {
            note_read_failure(a, a->op_service);
        }
        a->cursor++;
        return;

    case APPL_OP_WRITE: {
        /*
         * The standard defines success as a reply identical to the request.
         * It says nothing about what a differing reply means, so this reports
         * "not taken" rather than guessing at a reason.
         */
        bool echoed = resp->write &&
                      resp->service_id == a->op_service &&
                      resp->data_len >= 2u &&
                      resp->data[0] == (uint8_t)((a->op_value >> 8) & 0xFFu) &&
                      resp->data[1] == (uint8_t)(a->op_value & 0xFFu);

        if (echoed) {
            note_read_success(a, a->op_service, a->op_value, now_ms);
            finish_write_item(a, APPL_WRITE_OK);
        } else {
            finish_write_item(a, APPL_WRITE_NOT_TAKEN);
        }
        return;
    }

    case APPL_OP_NONE:
    default:
        return;
    }
}

/* ------------------------------------------------------------------- tick */

result_t appl_tick(appliance_t *a, link_t *link, int64_t now_ms)
{
    if (!a || !link)
        return RES_EINVAL;

    /* 1. Collect anything outstanding. */
    if (a->in_flight) {
        link_resp_t resp;
        result_t    r = link_poll(link, &resp);

        if (r == RES_OK) {
            handle_reply(a, link, &resp, now_ms);
            return RES_OK;
        }
        if (r == RES_EAGAIN) {
            if (now_ms >= a->stuck_deadline_ms) {
                /* The backstop: an adapter that never answers. */
                a->attempt = a->policy.max_retries + 1u;
                abandon(a, now_ms);
            }
            return RES_OK;
        }
        abandon(a, now_ms);
        return RES_OK;
    }

    /* 2. A link that is down overrides everything above it. */
    if (!link_is_up(link)) {
        if (a->state != APPL_DOWN)
            go_down(a, now_ms);
        return RES_OK;
    }

    /* 3. Identify the appliance before anything else.
     *
     * Both states are handled together so that IDENTIFYING with nothing
     * outstanding -- which is what a refused submission leaves behind -- is
     * self-healing rather than a state the model never leaves. */
    if (a->state == APPL_DOWN || a->state == APPL_IDENTIFYING) {
        if (now_ms < a->next_attempt_ms)
            return RES_OK;

        a->state   = APPL_IDENTIFYING;
        a->attempt = 0;
        (void)start(a, link, APPL_OP_REGISTER, 0, 0, now_ms);
        return RES_OK;
    }

    if (now_ms >= a->next_reregister_ms) {
        a->state   = APPL_IDENTIFYING;
        a->attempt = 0;
        (void)start(a, link, APPL_OP_REGISTER, 0, 0, now_ms);
        return RES_OK;
    }

    if (a->state != APPL_READY)
        return RES_OK;          /* identity only: nothing is pollable */

    /* 4. A control command outranks a sweep: it is what someone is waiting
     *    for, and a sweep can take twenty seconds. */
    while (a->write.active && a->write.cursor < a->write.n &&
           a->write.items[a->write.cursor].done)
        a->write.cursor++;

    if (a->write.active && a->write.cursor < a->write.n) {
        const appl_write_item_t *it = &a->write.items[a->write.cursor];

        a->attempt = 0;
        (void)start(a, link, APPL_OP_WRITE, it->service_id, it->value, now_ms);
        return RES_OK;
    }

    /* 5. Sweep the capability set, one service per transaction. */
    if (a->sweeping && a->cursor < a->reg.n_caps) {
        a->attempt = 0;
        (void)start(a, link, APPL_OP_READ,
                    a->reg.caps[a->cursor].service_id, 0, now_ms);
        return RES_OK;
    }

    if (a->sweeping) {
        /* The cursor reached the end: a full pass just finished, and only now
         * is there a complete set of values to report. */
        a->sweeping = false;
        a->sweeps++;
    }

    if (a->reg.n_caps > 0 && now_ms >= a->next_sweep_ms) {
        a->cursor   = 0;
        a->sweeping = true;
        /* Scheduled from the start of the sweep, so a sweep that outruns its
         * interval simply runs back to back instead of falling further and
         * further behind a schedule it cannot keep. */
        a->next_sweep_ms = now_ms + a->policy.sweep_interval_ms;
    }

    return RES_OK;
}

/* ------------------------------------------------------------------ writes */

result_t appl_write_begin(appliance_t *a, const appl_write_item_t *items,
                          size_t n)
{
    size_t i;

    if (!a || !items)
        return RES_EINVAL;
    if (n == 0 || n > APPL_WRITE_BATCH_MAX)
        return RES_ETOOBIG;
    if (a->write.active)
        return RES_EBUSY;
    if (a->state != APPL_READY)
        return RES_EIO;

    memset(&a->write, 0, sizeof(a->write));
    a->write.n      = n;
    a->write.active = true;

    for (i = 0; i < n; i++) {
        const ts_capability_t *cap;

        a->write.items[i].service_id = (uint8_t)(items[i].service_id &
                                                 TS_SVC_ID_MASK);
        a->write.items[i].value      = items[i].value;

        /* Resolve what the capability set already answers, without spending a
         * round trip on it. */
        cap = ts_registration_find(&a->reg, a->write.items[i].service_id);
        if (!cap) {
            a->write.items[i].result = APPL_WRITE_UNKNOWN;
            a->write.items[i].done   = true;
        } else if (!cap->writable) {
            a->write.items[i].result = APPL_WRITE_READ_ONLY;
            a->write.items[i].done   = true;
        }
    }

    return RES_OK;
}

bool appl_write_active(const appliance_t *a)
{
    return a && a->write.active;
}

bool appl_write_done(const appliance_t *a)
{
    size_t i;

    if (!a || !a->write.active)
        return false;

    for (i = 0; i < a->write.n; i++) {
        if (!a->write.items[i].done)
            return false;
    }
    return true;
}

const appl_write_batch_t *appl_write_batch(const appliance_t *a)
{
    if (!a || !a->write.active)
        return NULL;

    return &a->write;
}

void appl_write_clear(appliance_t *a)
{
    if (a)
        memset(&a->write, 0, sizeof(a->write));
}
