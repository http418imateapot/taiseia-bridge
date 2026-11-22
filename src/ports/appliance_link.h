/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The appliance link port.
 *
 * Neither call blocks. An adapter satisfies the contract however its target
 * allows -- a worker thread, a state machine pumped from the main loop, an
 * RTOS task, or an interrupt-driven transfer on bare metal. The core holds the
 * timeout and retry policy and never learns which of those is in use, so it
 * contains no concurrency primitive of any kind.
 *
 * See docs/adr/0001-appliance-link-is-a-port.md.
 */
#ifndef TSB_PORTS_APPLIANCE_LINK_H
#define TSB_PORTS_APPLIANCE_LINK_H

#include "core/taiseia/limits.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LINK_OP_READ = 0,
    LINK_OP_WRITE,
    LINK_OP_REGISTER   /* SA id 0x00: ask the appliance to describe itself */
} link_op_t;

/*
 * A request.
 *
 * **Zero the whole struct before filling it.** Fields are added here as the
 * protocol is covered, and every one of them defaults to off -- but only if it
 * was zeroed. A stale byte in `multibyte` turns an ordinary read into a
 * malformed one, which the adapters refuse loudly rather than send, though
 * that is a poor substitute for initialising it.
 */
typedef struct {
    uint32_t  id;          /* caller's correlation id, echoed in the reply  */
    link_op_t op;
    uint8_t   sa_id;
    uint16_t  service_id;  /* seven bits in the general format, sixteen in
                              the multi-byte one; never masked by an adapter */
    uint16_t  value;       /* LINK_OP_WRITE, general format only            */

    /*
     * The multi-byte format, used by meters and sensors. `data` is borrowed
     * for the duration of the submit call only: an adapter copies whatever it
     * needs before returning.
     */
    bool           multibyte;
    uint8_t        dtype;
    size_t         value_len;   /* one item's width, for a read             */
    uint16_t       count;       /* items, for a write                       */
    const uint8_t *data;        /* items, for a write                       */
    size_t         data_len;
} link_req_t;

typedef struct {
    uint32_t id;           /* the id of the request this answers            */
    result_t status;       /* RES_OK, or why the exchange failed            */
    uint8_t  sa_id;
    uint16_t service_id;   /* as the reply named it, all sixteen bits       */
    bool     write;

    /* Set when the reply came back in the multi-byte format. */
    bool     multibyte;
    uint8_t  dtype;
    uint32_t count;        /* items, summed across any fragments            */

    /* Sized for a reassembled reply, not for one packet: a fragmented answer
     * is joined up before anything above the transport sees it. */
    size_t   data_len;
    uint8_t  data[TS_REASSEMBLY_MAX];
} link_resp_t;

typedef struct {
    /** Accept a request. RES_EBUSY when the adapter has no room for it. */
    result_t (*submit)(void *self, const link_req_t *req);

    /** Take one completed reply. RES_EAGAIN when none is ready yet. */
    result_t (*poll)(void *self, link_resp_t *out);

    /** False once the underlying transport has failed. */
    bool (*is_up)(void *self);

    /** Release adapter resources. Safe to call on an already-closed link. */
    void (*close)(void *self);
} link_vt_t;

typedef struct {
    const link_vt_t *vt;
    void            *self;
} link_t;

result_t link_submit(link_t *l, const link_req_t *req);
result_t link_poll(link_t *l, link_resp_t *out);
bool     link_is_up(link_t *l);
void     link_close(link_t *l);

#endif /* TSB_PORTS_APPLIANCE_LINK_H */
