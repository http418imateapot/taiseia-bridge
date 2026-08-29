/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The POSIX serial port.
 *
 * Raw mode, no flow control, non-blocking reads. Everything a terminal driver
 * normally does for a human -- echo, line editing, translating CR to LF,
 * treating a byte as an interrupt -- would corrupt a binary protocol, so
 * cfmakeraw turns all of it off in one call rather than leaving a list of
 * flags to get individually right.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "adapters/platform.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static bool baud_constant(uint32_t baud, speed_t *out)
{
    switch (baud) {
    case 1200:  *out = B1200;  return true;
    case 2400:  *out = B2400;  return true;
    case 4800:  *out = B4800;  return true;
    case 9600:  *out = B9600;  return true;
    case 19200: *out = B19200; return true;
    case 38400: *out = B38400; return true;
    case 57600: *out = B57600; return true;
    default: break;
    }
    return false;
}

static void sp_close(void *self)
{
    platform_serial_t *s = (platform_serial_t *)self;

    if (s->handle >= 0)
        close((int)s->handle);

    s->handle = -1;
}

static result_t sp_open(void *self, const char *name,
                        const serial_params_t *p)
{
    platform_serial_t *s = (platform_serial_t *)self;
    struct termios     tio;
    speed_t            speed;
    int                fd;

    if (s->handle >= 0)
        return RES_EBUSY;
    if (!baud_constant(p->baud, &speed))
        return RES_EINVAL;
    if (p->data_bits != 8 || p->stop_bits != 1)
        return RES_EUNSUPPORTED;

    /* O_NOCTTY: this port must never become the process's controlling
     * terminal, or a stray hangup on it would signal us. */
    fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return (errno == ENOENT) ? RES_ENOTFOUND : RES_EIO;

    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return RES_EIO;
    }

    cfmakeraw(&tio);

    tio.c_cflag |= (CLOCAL | CREAD);      /* ignore modem lines, enable input */
    tio.c_cflag &= (tcflag_t)~CSTOPB;     /* one stop bit                     */
    tio.c_cflag &= (tcflag_t)~CSIZE;
    tio.c_cflag |= CS8;                   /* explicit, not inherited          */

    /* Hardware flow control is not part of the appliance link, and a cable
     * without those lines wired would stall the port forever with it on. */
#ifdef CRTSCTS
    tio.c_cflag &= (tcflag_t)~CRTSCTS;
#endif

    tio.c_cflag &= (tcflag_t)~PARENB;
    tio.c_cflag &= (tcflag_t)~PARODD;
    if (p->parity == 'E') {
        tio.c_cflag |= PARENB;
    } else if (p->parity == 'O') {
        tio.c_cflag |= (PARENB | PARODD);
    } else if (p->parity != 'N') {
        close(fd);
        return RES_EINVAL;
    }

    /* Return immediately with whatever is there. The caller owns the
     * timeouts, because only it knows what it is waiting for. */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0 ||
        tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return RES_EIO;
    }

    (void)tcflush(fd, TCIOFLUSH);

    s->handle = fd;
    return RES_OK;
}

static result_t sp_write(void *self, const uint8_t *buf, size_t n,
                         size_t *written)
{
    platform_serial_t *s = (platform_serial_t *)self;
    ssize_t            r;

    if (s->handle < 0)
        return RES_EIO;

    r = write((int)s->handle, buf, n);
    if (r > 0) {
        *written = (size_t)r;
        return RES_OK;
    }
    if (r == 0)
        return RES_EAGAIN;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return RES_EAGAIN;

    return RES_EIO;
}

static result_t sp_read(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    platform_serial_t *s = (platform_serial_t *)self;
    ssize_t            r;

    if (s->handle < 0)
        return RES_EIO;

    r = read((int)s->handle, buf, cap);
    if (r > 0) {
        *got = (size_t)r;
        return RES_OK;
    }
    /* Unlike a socket, zero from a serial port means nothing is waiting, not
     * that the far end has gone. A USB adapter being unplugged shows up as an
     * error instead. */
    if (r == 0)
        return RES_EAGAIN;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return RES_EAGAIN;

    return RES_EIO;
}

static result_t sp_flush(void *self)
{
    platform_serial_t *s = (platform_serial_t *)self;

    if (s->handle < 0)
        return RES_EIO;

    return (tcflush((int)s->handle, TCIOFLUSH) == 0) ? RES_OK : RES_EIO;
}

static bool sp_is_open(void *self)
{
    const platform_serial_t *s = (const platform_serial_t *)self;

    return s->handle >= 0;
}

static const serial_vt_t k_posix_serial_vt = {
    sp_open, sp_close, sp_write, sp_read, sp_flush, sp_is_open
};

void platform_serial_bind(platform_serial_t *s, serial_t *out)
{
    if (!s || !out)
        return;

    s->handle = -1;

    out->vt   = &k_posix_serial_vt;
    out->self = s;
}
