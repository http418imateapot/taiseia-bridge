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
 * The same applies to registration: the simulator assembles a real reply
 * payload out of its configured identity and services, so the decoder is
 * tested against bytes rather than against a struct someone filled in.
 *
 * It is also where faults are injected. Timeouts and corrupted checksums are
 * rare and unrepeatable on real hardware, so they are made deliberate here.
 */
#ifndef TSB_ADAPTERS_SIM_LINK_H
#define TSB_ADAPTERS_SIM_LINK_H

#include "core/taiseia/limits.h"
#include "core/taiseia/registration.h"
#include "core/util/result.h"
#include "ports/appliance_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  service_id;
    uint8_t  dtype;       /* a code from core/taiseia/datatype.h;
                             ignored in the general record format          */
    bool     writable;
    uint16_t value;
    bool     clamps;      /* store clamp_to instead of what was written    */
    uint16_t clamp_to;
    uint16_t items;       /* how many items a read of this service returns */
    uint8_t  support[TS_SUPPORT_MAX_BYTES];
    size_t   support_len;
} sim_service_t;

typedef struct {
    /* --- identity, reported at registration --- */
    ts_format_t   format;
    uint8_t       device_class;
    uint8_t       proto_major;
    uint8_t       proto_minor;
    uint8_t       baud_code;
    uint16_t      sa_id;
    char          make[TS_STRING_MAX + 1u];
    char          model[TS_STRING_MAX + 1u];

    size_t        n_services;
    sim_service_t services[TS_MAX_CAPABILITIES];

    /* When set, returned verbatim instead of an assembled payload. For
     * driving the decoder's refusals with bytes it should reject. */
    bool          raw_registration;
    uint8_t       raw[TS_DATA_MAX];
    size_t        raw_len;

    /* --- fault injection --- */
    unsigned      drop_next;      /* silently swallow this many requests   */
    bool          corrupt_next;   /* damage the next reply's checksum      */

    bool          up;
    bool          have_pending;
    link_resp_t   pending;
} sim_link_t;

/**
 * Reset to a general-format home appliance of type `sa_id`, with no services.
 * Make and model default to placeholders; override with sim_link_set_identity.
 */
void sim_link_init(sim_link_t *s, uint16_t sa_id);

/** Set what the appliance reports about itself. Strings are truncated to fit. */
result_t sim_link_set_identity(sim_link_t *s, ts_format_t format,
                               uint8_t device_class, uint16_t sa_id,
                               const char *make, const char *model);

/**
 * Add one service.
 *
 * `support` is the support datum exactly as it goes on the wire. In the
 * general record format it must be two bytes; in the multi-byte format it must
 * match the width the type code implies, which is what the decoder will
 * expect. RES_EINVAL if it does not.
 */
result_t sim_link_add_service(sim_link_t *s, uint8_t service_id, uint8_t dtype,
                              bool writable, uint16_t value,
                              const uint8_t *support, size_t support_len);

/** Return `blob` verbatim for a registration request, bypassing assembly. */
result_t sim_link_set_raw_registration(sim_link_t *s, const uint8_t *blob,
                                       size_t len);

/** Point `out` at this simulator. */
void sim_link_bind(sim_link_t *s, link_t *out);

/** Swallow the next `n` requests, so the caller sees a timeout. */
void sim_link_drop_next(sim_link_t *s, unsigned n);

/** Damage the checksum of the next reply. */
void sim_link_corrupt_next(sim_link_t *s);

/**
 * Make one service store `to` whatever is written to it, as an appliance that
 * clamps a setting to its own limits would. The reply then echoes a value the
 * requester did not send, which is the only signal the protocol gives that a
 * write was not taken as asked.
 */
result_t sim_link_set_clamp(sim_link_t *s, uint8_t service_id, uint16_t to);

/**
 * Make one service answer a read with `n` copies of its value.
 *
 * A multi-byte read asks for whatever the appliance has, not for a fixed
 * number, so a meter answering with a batch of samples is the normal case and
 * not an edge one. Ignored in the general record format, which has room for
 * exactly one two-byte value.
 */
result_t sim_link_set_items(sim_link_t *s, uint8_t service_id, uint16_t n);

/** Take the link down, as a pulled cable would. */
void sim_link_set_down(sim_link_t *s);

#endif /* TSB_ADAPTERS_SIM_LINK_H */
