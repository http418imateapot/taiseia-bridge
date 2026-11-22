/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/model/appliance.h"

#include "core/taiseia/datatype.h"

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
    return a && a->state == APPL_READY;
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

result_t appl_value(const appliance_t *a, uint16_t service_id, uint16_t *out)
{
    size_t i;

    if (!a || !out)
        return RES_EINVAL;

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

const appl_value_t *appl_reading(const appliance_t *a, uint16_t service_id)
{
    size_t i;

    if (!a)
        return NULL;

    for (i = 0; i < a->n_values; i++) {
        if (a->values[i].service_id != service_id)
            continue;
        return a->values[i].valid ? &a->values[i] : NULL;
    }
    return NULL;
}

const char *appl_state_str(appl_state_t s)
{
    switch (s) {
    case APPL_DOWN:                return "down";
    case APPL_IDENTIFYING:         return "identifying";
    case APPL_READY:               return "ready";
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
     * Both request formats are implemented, so every appliance that registers
     * is polled. A capability whose type has no fixed width cannot survive
     * registration decoding at all -- the record's own length depends on it --
     * so there is no half-known appliance to represent here.
     */
    a->state = APPL_READY;
}

/* ---------------------------------------------------------------- requests */

static void abandon(appliance_t *a, int64_t now_ms);

static result_t start(appliance_t *a, link_t *link, appl_op_t op,
                      uint16_t service_id, uint16_t value, int64_t now_ms)
{
    link_req_t req;
    result_t   r;

    memset(&req, 0, sizeof(req));
    req.id         = a->next_req_id++;
    req.sa_id      = a->wire_sa_id;
    req.service_id = service_id;
    req.value      = value;

    /*
     * A multi-byte request has to state the type and its width, both of which
     * the appliance told us at registration. Registration itself is in the
     * general shape whatever format everything else uses.
     */
    if (op != APPL_OP_REGISTER && a->reg.format == TS_FORMAT_MULTIBYTE) {
        const ts_capability_t *cap = ts_registration_find(&a->reg, service_id);
        const ts_datatype_t   *t   = (cap && cap->dtype_known)
                                         ? ts_datatype(cap->dtype) : NULL;

        if (!t || t->value_len == 0 || t->value_len > sizeof(a->wbuf))
            return RES_EUNSUPPORTED;

        req.multibyte = true;
        req.dtype     = cap->dtype;
        req.value_len = t->value_len;

        if (op == APPL_OP_WRITE) {
            /* One item, big endian, high bytes zero: the command carried a
             * two-byte value and widening it says nothing untrue. */
            memset(a->wbuf, 0, t->value_len);
            a->wbuf[t->value_len - 1u] = (uint8_t)(value & 0xFFu);
            if (t->value_len >= 2u)
                a->wbuf[t->value_len - 2u] = (uint8_t)((value >> 8) & 0xFFu);

            req.count    = 1;
            req.data     = a->wbuf;
            req.data_len = t->value_len;
        }
    }

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

static void note_read_failure(appliance_t *a, uint16_t service_id)
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

static appl_value_t *value_slot(appliance_t *a, uint16_t service_id)
{
    size_t i;

    for (i = 0; i < a->n_values; i++) {
        if (a->values[i].service_id == service_id)
            return &a->values[i];
    }
    return NULL;
}

static void note_read_success(appliance_t *a, uint16_t service_id,
                              uint16_t value, int64_t now_ms)
{
    appl_value_t *v = value_slot(a, service_id);

    if (!v)
        return;

    v->value      = value;
    v->multibyte  = false;
    v->raw_len    = 0;
    v->count      = 1;
    v->valid      = true;
    v->misses     = 0;
    v->updated_ms = now_ms;
}

/* A reading wider than two bytes, kept as it arrived. */
static void note_read_success_mb(appliance_t *a, const link_resp_t *resp,
                                 int64_t now_ms)
{
    appl_value_t *v = value_slot(a, resp->service_id);

    if (!v)
        return;

    if (resp->data_len > sizeof(v->raw)) {
        /* Refused rather than truncated: a short batch of readings looks
         * exactly like a complete one. */
        v->misses++;
        if (v->misses >= a->policy.max_misses)
            v->valid = false;
        return;
    }

    v->multibyte  = true;
    v->dtype      = resp->dtype;
    v->count      = resp->count;
    v->raw_len    = resp->data_len;
    if (resp->data_len > 0)
        memcpy(v->raw, resp->data, resp->data_len);

    /*
     * Keep a narrow view too, for callers that only want a number: the low
     * two bytes of the FIRST item. A batch has no single value, and the first
     * reading is the least arbitrary answer to a question that assumes one.
     */
    {
        size_t item = (resp->count > 0u &&
                       resp->data_len % resp->count == 0u)
                          ? resp->data_len / resp->count
                          : resp->data_len;

        if (item >= 2u)
            v->value = (uint16_t)(((uint16_t)resp->data[item - 2u] << 8) |
                                  resp->data[item - 1u]);
        else if (item == 1u)
            v->value = resp->data[0];
        else
            v->value = 0u;
    }

    v->valid      = true;
    v->misses     = 0;
    v->updated_ms = now_ms;
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
        if (resp->multibyte) {
            /* The reading is filed under the id the reply names, so a reply
             * naming some other service than the one asked about -- 0x20 in
             * answer to 0x0120 -- is a failed read, not a reading of 0x20. */
            if (resp->service_id == a->op_service)
                note_read_success_mb(a, resp, now_ms);
            else
                note_read_failure(a, a->op_service);
        } else if (resp->data_len >= 2u) {
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
        /* The value sits at the end of the data field in both formats: the
         * general one has exactly two bytes, and a widened multi-byte item
         * has its low bytes last. */
        bool echoed = resp->write &&
                      resp->service_id == a->op_service &&
                      resp->data_len >= 2u &&
                      resp->data[resp->data_len - 2u] ==
                          (uint8_t)((a->op_value >> 8) & 0xFFu) &&
                      resp->data[resp->data_len - 1u] ==
                          (uint8_t)(a->op_value & 0xFFu);

        if (echoed) {
            /* Cache the echo in the shape it arrived in, so a service does
             * not change shape between a write and the next sweep. */
            if (resp->multibyte)
                note_read_success_mb(a, resp, now_ms);
            else
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

        /* Taken whole. An id the appliance did not declare -- including
         * anything above 0x7F on a general-format appliance -- is `unknown`
         * below, never masked into one it did. */
        a->write.items[i].service_id = items[i].service_id;
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
