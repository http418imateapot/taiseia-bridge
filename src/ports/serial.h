/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The byte-level serial port.
 *
 * This is the lower of the two seams. Packet framing does NOT live behind it:
 * framing is pure code in core/taiseia, so it is unit-tested once rather than
 * reimplemented and retested per platform. A serial adapter therefore has
 * nothing to get subtly wrong beyond opening the device and honouring a
 * timeout.
 *
 * No adapter implements this yet -- see docs/porting.md.
 *
 * Port naming is platform-shaped and is passed through verbatim:
 *   POSIX     /dev/ttyUSB0
 *   Windows   COM3, and \\.\COM10 and above -- the bare form silently fails
 *             for two-digit port numbers.
 */
#ifndef TSB_PORTS_SERIAL_H
#define TSB_PORTS_SERIAL_H

#include "core/util/result.h"

#include <stddef.h>
#include <stdint.h>

typedef struct serial_port serial_port_t;   /* defined by the adapter */

typedef struct {
    uint32_t baud;        /* 1200 .. 57600; appliances default to 9600     */
    uint8_t  data_bits;   /* 8                                             */
    char     parity;      /* 'N', 'E' or 'O'                               */
    uint8_t  stop_bits;   /* 1                                             */
} serial_params_t;

result_t serial_open(serial_port_t **out, const char *name,
                     const serial_params_t *params);

void serial_close(serial_port_t *sp);

result_t serial_write(serial_port_t *sp, const uint8_t *buf, size_t n,
                      size_t *written);

/**
 * Read up to `cap` bytes, returning as soon as any arrive.
 *
 * RES_ETIMEOUT when nothing arrives before the deadline. The caller loops
 * until it holds the whole packet, because the number of bytes a read returns
 * says nothing about where a packet ends.
 */
result_t serial_read(serial_port_t *sp, uint8_t *buf, size_t cap,
                     int timeout_ms, size_t *got);

/** Discard anything buffered in either direction. */
result_t serial_flush(serial_port_t *sp);

#endif /* TSB_PORTS_SERIAL_H */
