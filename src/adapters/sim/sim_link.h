/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A simulated appliance behind the appliance link port.
 *
 * The simulator is a peer of the real adapters, not a shortcut around them: it
 * encodes and decodes genuine packets, so every exchange through it runs the
 * same framing and checksum code a wire would. That is what makes it usable as
 * the project's primary verification vehicle in the absence of hardware, and
 * what stops the framing layer from being exercised only by tests written
 * against itself.
 *
 * It is also where faults are injected. Timeouts and corrupted checksums are
 * rare and unrepeatable on real hardware, so they are made deliberate here.
 */
#ifndef TSB_ADAPTERS_SIM_LINK_H
#define TSB_ADAPTERS_SIM_LINK_H

#include "core/taiseia/limits.h"
#include "core/util/result.h"
#include "ports/appliance_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  service_id;
    uint8_t  dtype;       /* a code from core/taiseia/datatype.h           */
    bool     writable;
    uint16_t value;
    uint16_t support_hi;  /* minimum, for a range type                    */
    uint16_t support_lo;  /* maximum, or the mask for a bit type          */
} sim_service_t;

typedef struct {
    uint8_t       sa_id;
    size_t        n_services;
    sim_service_t services[TS_MAX_CAPABILITIES];

    /* Answered verbatim for LINK_OP_REGISTER. The registration payload
     * layout is decoded elsewhere; the simulator only has to deliver it. */
    uint8_t       reg_blob[TS_DATA_MAX];
    size_t        reg_blob_len;

    /* Fault injection. */
    unsigned      drop_next;      /* silently swallow this many requests   */
    bool          corrupt_next;   /* damage the next reply's checksum      */

    bool          up;
    bool          have_pending;
    link_resp_t   pending;
} sim_link_t;

/** Reset to an appliance of type `sa_id` with no services. */
void sim_link_init(sim_link_t *s, uint8_t sa_id);

/** Add one service. RES_ETOOBIG past TS_MAX_CAPABILITIES. */
result_t sim_link_add_service(sim_link_t *s, uint8_t service_id, uint8_t dtype,
                              bool writable, uint16_t value,
                              uint16_t support_hi, uint16_t support_lo);

/** Set the bytes returned for a registration request. */
result_t sim_link_set_registration(sim_link_t *s, const uint8_t *blob,
                                   size_t len);

/** Point `out` at this simulator. */
void sim_link_bind(sim_link_t *s, link_t *out);

/** Swallow the next `n` requests, so the caller sees a timeout. */
void sim_link_drop_next(sim_link_t *s, unsigned n);

/** Damage the checksum of the next reply. */
void sim_link_corrupt_next(sim_link_t *s);

/** Take the link down, as a cable pull would. */
void sim_link_set_down(sim_link_t *s);

#endif /* TSB_ADAPTERS_SIM_LINK_H */
