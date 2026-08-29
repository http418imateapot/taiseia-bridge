/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The appliance model: what we know about the device, and what to ask it next.
 *
 * This is the only authority on whether an appliance is online, what it can
 * do, and what its services currently read. Everything above it -- MQTT, the
 * command-line tool -- reads from here rather than talking to the link.
 *
 * TIME IS A PARAMETER, NOT A PORT. `now_ms` is threaded through from the
 * caller, so the core needs no clock and every scheduling rule can be tested
 * by passing numbers rather than by faking a clock.
 *
 * WHO OWNS WHICH DEADLINE. The 200 ms response deadline is measured from the
 * last byte of the request leaving the wire, which only an adapter can know --
 * timing it from submit() would include queueing delay on a threaded adapter
 * and measure the wrong thing. Adapters enforce it and report RES_ETIMEOUT.
 * Everything above that -- how many times to retry, when to re-register, when
 * to declare the appliance offline -- is here. See docs/adr/0001.
 */
#ifndef TSB_CORE_MODEL_APPLIANCE_H
#define TSB_CORE_MODEL_APPLIANCE_H

#include "core/taiseia/limits.h"
#include "core/taiseia/registration.h"
#include "core/util/result.h"
#include "ports/appliance_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APPL_WRITE_BATCH_MAX  8u

/*
 * A backstop, not the protocol deadline. An adapter that never answers a poll
 * would otherwise wedge the model for good; this puts a ceiling on that. It is
 * deliberately far above any real exchange so that it never fires in normal
 * operation and never competes with the adapter's own timing.
 */
#define APPL_STUCK_DEADLINE_MS  5000

typedef enum {
    APPL_DOWN = 0,            /* no usable link, or nothing has answered   */
    APPL_IDENTIFYING,         /* a registration request is outstanding     */
    APPL_READY,               /* identity known, services being polled     */
    APPL_READY_IDENTITY_ONLY  /* identity known, services not pollable     */
} appl_state_t;

/*
 * Write outcomes.
 *
 * These are our own vocabulary. The device protocol has no result codes for a
 * write: on success the appliance returns a reply identical to the request,
 * and the standard says nothing about what a differing reply means. Mapping
 * these to whatever numbers a particular backend expects belongs to the
 * northbound layer, in configuration. See docs/adr/0011.
 */
typedef enum {
    APPL_WRITE_OK = 0,      /* the reply matched the request byte for byte */
    APPL_WRITE_UNKNOWN,     /* the service is not in the capability set    */
    APPL_WRITE_READ_ONLY,   /* it is, but the appliance declared it read   */
    APPL_WRITE_NOT_TAKEN,   /* the appliance replied with a different value */
    APPL_WRITE_NO_REPLY     /* timed out, or the transport failed          */
} appl_write_result_t;

typedef struct {
    uint8_t             service_id;
    uint16_t            value;
    appl_write_result_t result;
    bool                done;
} appl_write_item_t;

typedef struct {
    size_t            n;
    size_t            cursor;
    bool              active;
    appl_write_item_t items[APPL_WRITE_BATCH_MAX];
} appl_write_batch_t;

typedef struct {
    uint8_t  service_id;
    uint16_t value;
    bool     valid;
    int64_t  updated_ms;
    unsigned misses;        /* consecutive failed reads of this service */
} appl_value_t;

typedef struct {
    int      sweep_interval_ms;       /* between the starts of two sweeps  */
    int      reregister_interval_ms;  /* how often to re-read capabilities */
    unsigned max_retries;             /* per transaction; the standard says 3 */
    unsigned max_misses;              /* failures before a value goes stale,
                                         and before the appliance is down  */
    int      retry_backoff_min_ms;
    int      retry_backoff_max_ms;
} appl_policy_t;

/** Defaults: a 60 s sweep, capabilities re-read every 5 minutes. */
void appl_policy_defaults(appl_policy_t *p);

typedef enum {
    APPL_OP_NONE = 0,
    APPL_OP_REGISTER,
    APPL_OP_READ,
    APPL_OP_WRITE
} appl_op_t;

typedef struct {
    appl_policy_t     policy;
    appl_state_t      state;

    ts_registration_t reg;
    bool              reg_valid;
    uint8_t           wire_sa_id;   /* low byte of the declared appliance type */

    size_t            n_values;
    appl_value_t      values[TS_MAX_CAPABILITIES];

    /* scheduling */
    size_t            cursor;          /* next capability in this sweep     */
    bool              sweeping;
    int64_t           next_sweep_ms;
    int64_t           next_reregister_ms;
    int64_t           next_attempt_ms; /* backoff gate after a failed cycle */
    int               backoff_ms;

    /* in flight */
    bool              in_flight;
    appl_op_t         op;
    uint32_t          req_id;
    uint32_t          next_req_id;
    uint8_t           op_service;
    uint16_t          op_value;
    unsigned          attempt;
    int64_t           stuck_deadline_ms;

    unsigned          consecutive_failures;

    appl_write_batch_t write;
} appliance_t;

void appl_init(appliance_t *a, const appl_policy_t *policy);

/**
 * Advance the model by one step. Never blocks; call it as often as the caller
 * likes. At most one link transaction is started or collected per call, which
 * keeps the work per iteration bounded and the sequencing easy to follow.
 */
result_t appl_tick(appliance_t *a, link_t *link, int64_t now_ms);

appl_state_t appl_state(const appliance_t *a);

/** True once the appliance has been identified and is answering. */
bool appl_online(const appliance_t *a);

/** The registration data, or NULL before the appliance has been identified. */
const ts_registration_t *appl_identity(const appliance_t *a);

/** Latest value of one service. RES_ENOTFOUND when absent or stale. */
result_t appl_value(const appliance_t *a, uint8_t service_id, uint16_t *out);

/**
 * Start a batch of writes, as one control command asks for.
 *
 * Items that fail a capability check are resolved here without touching the
 * link. RES_EBUSY if a batch is already running, RES_EIO if the appliance is
 * not ready, RES_ETOOBIG past APPL_WRITE_BATCH_MAX.
 */
result_t appl_write_begin(appliance_t *a, const appl_write_item_t *items,
                          size_t n);

bool appl_write_active(const appliance_t *a);
bool appl_write_done(const appliance_t *a);

/** The batch, including per-item results. NULL when none has been started. */
const appl_write_batch_t *appl_write_batch(const appliance_t *a);

/** Discard a finished batch, so the next command can start. */
void appl_write_clear(appliance_t *a);

const char *appl_state_str(appl_state_t s);
const char *appl_write_result_str(appl_write_result_t r);

#endif /* TSB_CORE_MODEL_APPLIANCE_H */
