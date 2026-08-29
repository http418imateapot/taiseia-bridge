/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The byte-level serial port.
 *
 * This is the lower of the two southbound seams. Packet framing does NOT live
 * behind it: framing is pure code in core/taiseia, so it is written once and
 * tested once rather than reimplemented per platform. A serial adapter
 * therefore has nothing to get subtly wrong beyond opening the device and not
 * blocking.
 *
 * NOTHING BLOCKS, as everywhere else. `read` returns what is waiting, or
 * RES_EAGAIN. It must not try to return a whole packet: how many bytes a read
 * returns says nothing about where a packet ends, and a caller that assumes
 * otherwise will stall the moment a reply arrives in two pieces.
 *
 * Port naming is platform-shaped and passed through verbatim:
 *   POSIX     /dev/ttyUSB0
 *   Windows   COM3, and \\.\COM10 and above -- the bare form silently fails
 *             for two-digit port numbers, which is a memorable afternoon.
 */
#ifndef TSB_PORTS_SERIAL_H
#define TSB_PORTS_SERIAL_H

#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t baud;        /* 1200 .. 57600; appliances default to 9600 */
    uint8_t  data_bits;   /* 8 */
    char     parity;      /* 'N', 'E' or 'O' */
    uint8_t  stop_bits;   /* 1 */
} serial_params_t;

/** 9600 8N1, which is what the appliance protocol specifies by default. */
void serial_params_defaults(serial_params_t *p);

typedef struct {
    result_t (*open)(void *self, const char *name, const serial_params_t *p);
    void     (*close)(void *self);

    /** Write what fits. `*written` may be less than `n`, including zero. */
    result_t (*write)(void *self, const uint8_t *buf, size_t n,
                      size_t *written);

    /** Read what is waiting. RES_EAGAIN when nothing is. */
    result_t (*read)(void *self, uint8_t *buf, size_t cap, size_t *got);

    /** Discard anything buffered in either direction. */
    result_t (*flush)(void *self);

    bool (*is_open)(void *self);
} serial_vt_t;

typedef struct {
    const serial_vt_t *vt;
    void              *self;
} serial_t;

result_t serial_open(serial_t *s, const char *name, const serial_params_t *p);
void     serial_close(serial_t *s);
result_t serial_write(serial_t *s, const uint8_t *buf, size_t n,
                      size_t *written);
result_t serial_read(serial_t *s, uint8_t *buf, size_t cap, size_t *got);
result_t serial_flush(serial_t *s);
bool     serial_is_open(serial_t *s);

#endif /* TSB_PORTS_SERIAL_H */
