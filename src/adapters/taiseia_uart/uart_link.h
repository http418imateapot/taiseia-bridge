/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The appliance link over a real UART.
 *
 * This is the `inline` strategy from ADR 0001: no thread, no blocking call.
 * `submit` starts an exchange and `poll` advances a small state machine one
 * step -- write the request, gather the reply, decide. That is possible here
 * only because the framing above it is pure code, so this file has nothing to
 * do but move bytes and watch two clocks.
 *
 * THE TWO DEADLINES, both from the standard:
 *
 *   200 ms  from the end of the request to the start of the reply. Ours to
 *           enforce, because only an adapter knows when the last byte left.
 *   30 ms   between two bytes of one packet. A longer gap means the packet is
 *           not one packet, and continuing to accumulate would eventually
 *           assemble a plausible frame out of two halves.
 *
 * Retries are NOT here. The core decides how many times to ask again, so that
 * two adapters cannot disagree about how many times three is.
 *
 * The port is flushed at the start of every exchange. A reply that arrived
 * after we gave up on it would otherwise be read as the answer to the next
 * question, and every value after that would be attributed to the wrong
 * service.
 */
#ifndef TSB_ADAPTERS_UART_LINK_H
#define TSB_ADAPTERS_UART_LINK_H

#include "core/taiseia/limits.h"
#include "core/util/result.h"
#include "ports/appliance_link.h"
#include "ports/serial.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    UL_IDLE = 0,
    UL_WRITING,
    UL_READING,
    UL_DONE
} ul_phase_t;

typedef struct {
    serial_t   *sp;
    int64_t   (*now_ms)(void);

    ul_phase_t  phase;
    uint32_t    req_id;

    uint8_t     req[TS_GENERAL_REQ_LEN];
    size_t      req_len;
    size_t      req_sent;

    uint8_t     rx[TS_FRAME_MAX_LEN];
    size_t      rx_len;

    int64_t     deadline_ms;
    int64_t     last_byte_ms;

    link_resp_t result;
} uart_link_t;

/**
 * `now_ms` is required, not optional: it is what makes the two deadlines
 * testable by passing numbers instead of by sleeping. Applications pass
 * platform_now_ms.
 */
result_t uart_link_init(uart_link_t *u, serial_t *sp, int64_t (*now_ms)(void));

void uart_link_bind(uart_link_t *u, link_t *out);

#endif /* TSB_ADAPTERS_UART_LINK_H */
