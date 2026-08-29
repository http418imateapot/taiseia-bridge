/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Windows serial port.
 *
 * Same shape as the POSIX file, so the two read as a pair. Two things differ
 * and both are Windows-specific traps:
 *
 *  - COM10 and above must be opened as \\.\COM10. The bare name silently
 *    fails, and it fails only on machines with enough USB adapters to have
 *    reached double digits, which is a memorable afternoon. The name is
 *    prefixed here rather than left to whoever writes the configuration file.
 *
 *  - The read timeouts have to be set to return immediately with whatever is
 *    buffered. The default blocks, and one blocking read would stall the whole
 *    program.
 */
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

#include "adapters/platform.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#define HANDLE_CLOSED ((intptr_t)-1)

static HANDLE as_handle(intptr_t h)
{
    return (HANDLE)h;
}

/* COM1..COM9 work bare; anything longer needs the device-namespace prefix,
 * and adding it to a name that already has it is harmless. */
static void full_name(char *out, size_t cap, const char *name)
{
    if (name[0] == '\\' || (name[0] != 'C' && name[0] != 'c')) {
        snprintf(out, cap, "%s", name);
        return;
    }
    snprintf(out, cap, "\\\\.\\%s", name);
}

static void sw_close(void *self)
{
    platform_serial_t *s = (platform_serial_t *)self;

    if (s->handle != HANDLE_CLOSED)
        CloseHandle(as_handle(s->handle));

    s->handle = HANDLE_CLOSED;
}

static result_t sw_open(void *self, const char *name,
                        const serial_params_t *p)
{
    platform_serial_t *s = (platform_serial_t *)self;
    char               path[64];
    HANDLE             h;
    DCB                dcb;
    COMMTIMEOUTS       to;

    if (s->handle != HANDLE_CLOSED)
        return RES_EBUSY;
    if (p->data_bits != 8 || p->stop_bits != 1)
        return RES_EUNSUPPORTED;
    if (p->baud < 1200 || p->baud > 57600)
        return RES_EINVAL;

    full_name(path, sizeof(path), name);

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return (GetLastError() == ERROR_FILE_NOT_FOUND) ? RES_ENOTFOUND
                                                        : RES_EIO;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return RES_EIO;
    }

    dcb.BaudRate = (DWORD)p->baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;

    if (p->parity == 'E') {
        dcb.Parity  = EVENPARITY;
        dcb.fParity = TRUE;
    } else if (p->parity == 'O') {
        dcb.Parity  = ODDPARITY;
        dcb.fParity = TRUE;
    } else if (p->parity == 'N') {
        dcb.Parity  = NOPARITY;
        dcb.fParity = FALSE;
    } else {
        CloseHandle(h);
        return RES_EINVAL;
    }

    /* No flow control: the appliance link does not use it, and a cable
     * without those lines wired would stall the port forever with it on. */
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDtrControl     = DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = RTS_CONTROL_DISABLE;
    dcb.fOutX           = FALSE;
    dcb.fInX            = FALSE;
    dcb.fBinary         = TRUE;
    dcb.fAbortOnError   = FALSE;
    dcb.fNull           = FALSE;      /* keep NUL bytes: this is binary */

    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return RES_EIO;
    }

    /* MAXDWORD interval with zero totals is the documented way to say
     * "return whatever is buffered, immediately". */
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 1000;

    if (!SetCommTimeouts(h, &to)) {
        CloseHandle(h);
        return RES_EIO;
    }

    (void)PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    s->handle = (intptr_t)h;
    return RES_OK;
}

static result_t sw_write(void *self, const uint8_t *buf, size_t n,
                         size_t *written)
{
    platform_serial_t *s = (platform_serial_t *)self;
    DWORD              wrote = 0;

    if (s->handle == HANDLE_CLOSED)
        return RES_EIO;
    if (n > 0xFFFFFFFFu)
        n = 0xFFFFFFFFu;

    if (!WriteFile(as_handle(s->handle), buf, (DWORD)n, &wrote, NULL))
        return RES_EIO;

    *written = (size_t)wrote;

    return (wrote > 0) ? RES_OK : RES_EAGAIN;
}

static result_t sw_read(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    platform_serial_t *s = (platform_serial_t *)self;
    DWORD              read_n = 0;

    if (s->handle == HANDLE_CLOSED)
        return RES_EIO;
    if (cap > 0xFFFFFFFFu)
        cap = 0xFFFFFFFFu;

    if (!ReadFile(as_handle(s->handle), buf, (DWORD)cap, &read_n, NULL))
        return RES_EIO;

    /* Zero means nothing was waiting, not that the far end has gone -- an
     * adapter being unplugged surfaces as a failed ReadFile instead. */
    if (read_n == 0)
        return RES_EAGAIN;

    *got = (size_t)read_n;
    return RES_OK;
}

static result_t sw_flush(void *self)
{
    platform_serial_t *s = (platform_serial_t *)self;

    if (s->handle == HANDLE_CLOSED)
        return RES_EIO;

    return PurgeComm(as_handle(s->handle), PURGE_RXCLEAR | PURGE_TXCLEAR)
               ? RES_OK : RES_EIO;
}

static bool sw_is_open(void *self)
{
    const platform_serial_t *s = (const platform_serial_t *)self;

    return s->handle != HANDLE_CLOSED;
}

static const serial_vt_t k_win32_serial_vt = {
    sw_open, sw_close, sw_write, sw_read, sw_flush, sw_is_open
};

void platform_serial_bind(platform_serial_t *s, serial_t *out)
{
    if (!s || !out)
        return;

    s->handle = HANDLE_CLOSED;

    out->vt   = &k_win32_serial_vt;
    out->self = s;
}
