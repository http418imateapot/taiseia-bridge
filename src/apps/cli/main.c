/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * taiseia-cli: talk to an appliance by hand.
 *
 * It speaks exactly the protocol the bridge speaks, through exactly the same
 * adapter, so anything the bridge does can be reproduced here one exchange at
 * a time. When something in the field is wrong, that is the difference between
 * guessing and looking.
 */
#include "adapters/platform.h"
#include "adapters/sim/sim_link.h"
#include "adapters/taiseia_uart/uart_link.h"
#include "core/taiseia/datatype.h"
#include "core/taiseia/registration.h"
#include "core/util/result.h"
#include "ports/appliance_link.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCHANGE_BUDGET_MS 2000

static platform_serial_t g_pser;
static serial_t          g_serial;
static uart_link_t       g_uart;
static sim_link_t        g_sim;

static void usage(const char *prog)
{
    printf("taiseia-cli %s\n\n", TSB_VERSION);
    printf("usage: %s [options] <command>\n\n", prog);
    printf("  -p, --port PATH    serial port (/dev/ttyUSB0, COM3)\n");
    printf("  -b, --baud N       1200 .. 57600, default 9600\n");
    printf("      --link sim     talk to the built-in simulator instead\n");
    printf("  -h, --help\n\n");
    printf("commands:\n");
    printf("  identify                    ask the appliance what it is\n");
    printf("  read  <sa> <service>        read one service\n");
    printf("  write <sa> <service> <val>  write one service\n\n");
    printf("Numbers may be decimal or 0x-prefixed hex.\n");
}

static bool parse_num(const char *s, unsigned long *out, unsigned long max)
{
    char         *end = NULL;
    unsigned long v;

    if (!s || !*s)
        return false;

    v = strtoul(s, &end, 0);
    if (!end || *end != '\0' || v > max)
        return false;

    *out = v;
    return true;
}

/* One request, one reply, or a reason there was not one. */
static result_t exchange(link_t *l, const link_req_t *req, link_resp_t *resp)
{
    int64_t deadline;
    result_t r;

    r = link_submit(l, req);
    if (r != RES_OK)
        return r;

    deadline = platform_now_ms() + EXCHANGE_BUDGET_MS;

    for (;;) {
        r = link_poll(l, resp);
        if (r != RES_EAGAIN)
            return r;
        if (platform_now_ms() >= deadline)
            return RES_ETIMEOUT;
        platform_sleep_ms(2);
    }
}

static void print_capability(const ts_capability_t *c)
{
    size_t i;

    printf("  service %-3u  %-2s  ", (unsigned)c->service_id,
           c->writable ? "rw" : "r");

    if (c->dtype_known)
        printf("%-22s ", ts_datatype_name(c->dtype));
    else
        printf("%-22s ", "(no type on the wire)");

    printf("support ");
    for (i = 0; i < c->support_len; i++)
        printf("%02x", c->support[i]);
    printf("\n");
}

static int do_identify(link_t *l)
{
    link_req_t        req;
    link_resp_t       resp;
    ts_registration_t reg;
    result_t          r;
    size_t            i;

    memset(&req, 0, sizeof(req));
    req.id = 1;
    req.op = LINK_OP_REGISTER;

    r = exchange(l, &req, &resp);
    if (r != RES_OK) {
        fprintf(stderr, "registration failed: %s\n", res_str(r));
        return 1;
    }
    if (resp.status != RES_OK) {
        fprintf(stderr, "appliance did not register: %s\n",
                res_str(resp.status));
        return 1;
    }

    r = ts_registration_parse(resp.data, resp.data_len, &reg);
    if (r != RES_OK) {
        fprintf(stderr, "registration reply not understood: %s\n", res_str(r));
        if (r == RES_EUNSUPPORTED)
            fprintf(stderr, "  (a fragmented reply, or a type code with no "
                            "fixed width -- neither is implemented)\n");
        return 1;
    }

    printf("appliance type  0x%04X\n", (unsigned)reg.sa_id);
    printf("make            %s\n", reg.make);
    printf("model           %s\n", reg.model);
    printf("protocol        %u.%u\n", (unsigned)reg.proto_major,
           (unsigned)reg.proto_minor);
    printf("link speed      %u bps\n", (unsigned)ts_baud_from_code(reg.baud_code));
    printf("record format   %s\n",
           (reg.format == TS_FORMAT_MULTIBYTE) ? "multi-byte" : "general");
    printf("device class    %u\n", (unsigned)reg.device_class);
    printf("services        %u\n", (unsigned)reg.n_caps);

    for (i = 0; i < reg.n_caps; i++)
        print_capability(&reg.caps[i]);

    if (reg.format == TS_FORMAT_GENERAL && reg.n_caps > 0) {
        printf("\nThe general record format carries no data type, so the two\n"
               "support bytes could be a range, a maximum or a bit mask. What\n"
               "they are is in the service tables, which are not shipped here.\n");
    }

    return 0;
}

static int do_read(link_t *l, uint8_t sa, uint8_t service)
{
    link_req_t  req;
    link_resp_t resp;
    result_t    r;

    memset(&req, 0, sizeof(req));
    req.id = 2;
    req.op = LINK_OP_READ;
    req.sa_id = sa;
    req.service_id = service;

    r = exchange(l, &req, &resp);
    if (r != RES_OK) {
        fprintf(stderr, "read failed: %s\n", res_str(r));
        return 1;
    }
    if (resp.status != RES_OK) {
        fprintf(stderr, "no value: %s\n", res_str(resp.status));
        return 1;
    }
    if (resp.data_len < 2) {
        fprintf(stderr, "reply too short\n");
        return 1;
    }

    printf("service %u = %u (0x%02X%02X)\n", (unsigned)resp.service_id,
           (unsigned)((resp.data[0] << 8) | resp.data[1]),
           resp.data[0], resp.data[1]);

    return 0;
}

static int do_write(link_t *l, uint8_t sa, uint8_t service, uint16_t value)
{
    link_req_t  req;
    link_resp_t resp;
    result_t    r;
    unsigned    echoed;

    memset(&req, 0, sizeof(req));
    req.id = 3;
    req.op = LINK_OP_WRITE;
    req.sa_id = sa;
    req.service_id = service;
    req.value = value;

    r = exchange(l, &req, &resp);
    if (r != RES_OK) {
        fprintf(stderr, "write failed: %s\n", res_str(r));
        return 1;
    }
    if (resp.status != RES_OK) {
        fprintf(stderr, "no reply: %s\n", res_str(resp.status));
        return 1;
    }
    if (resp.data_len < 2) {
        fprintf(stderr, "reply too short\n");
        return 1;
    }

    echoed = (unsigned)((resp.data[0] << 8) | resp.data[1]);
    if (echoed == value && resp.write) {
        printf("service %u written: %u\n", (unsigned)service,
               (unsigned)value);
        return 0;
    }

    /* Success is a reply identical to the request. Anything else means the
     * write was not taken as asked, and the protocol does not say why. */
    printf("service %u NOT taken: asked for %u, appliance replied %u\n",
           (unsigned)service, (unsigned)value, echoed);

    return 1;
}

static const uint8_t k_sup[] = { 0x00, 0x64 };

static result_t open_sim(link_t *l)
{
    sim_link_init(&g_sim, 0x04);
    if (sim_link_set_identity(&g_sim, TS_FORMAT_GENERAL, TS_CLASS_APPLIANCE,
                              0x0004, "SIMULATOR", "SIM-0001") != RES_OK ||
        sim_link_add_service(&g_sim, 0x00, 0, true,  1,  k_sup, 2) != RES_OK ||
        sim_link_add_service(&g_sim, 0x03, 0, true,  55, k_sup, 2) != RES_OK ||
        sim_link_add_service(&g_sim, 0x07, 0, false, 62, k_sup, 2) != RES_OK)
        return RES_EINVAL;

    sim_link_bind(&g_sim, l);
    return RES_OK;
}

static result_t open_uart(link_t *l, const char *port, uint32_t baud)
{
    serial_params_t p;
    result_t        r;

    if (!port) {
        fprintf(stderr, "no serial port given; use --port or --link sim\n");
        return RES_EINVAL;
    }

    serial_params_defaults(&p);
    p.baud = baud;

    platform_serial_bind(&g_pser, &g_serial);

    r = serial_open(&g_serial, port, &p);
    if (r != RES_OK) {
        fprintf(stderr, "cannot open %s: %s\n", port, res_str(r));
        return r;
    }

    return uart_link_init(&g_uart, &g_serial, platform_now_ms);
}

static bool arg_is(const char *a, const char *shrt, const char *lng)
{
    return (shrt && strcmp(a, shrt) == 0) || (lng && strcmp(a, lng) == 0);
}

int main(int argc, char *argv[])
{
    const char   *port = NULL;
    unsigned long baud = 9600;
    bool          use_sim = false;
    link_t        link;
    int           i;
    int           rc;

    memset(&link, 0, sizeof(link));

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (arg_is(a, "-h", "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (arg_is(a, "-p", "--port") && i + 1 < argc) {
            port = argv[++i];
            continue;
        }
        if (arg_is(a, "-b", "--baud") && i + 1 < argc) {
            if (!parse_num(argv[++i], &baud, 57600)) {
                fprintf(stderr, "bad baud rate\n");
                return 2;
            }
            continue;
        }
        if (arg_is(a, NULL, "--link") && i + 1 < argc) {
            use_sim = (strcmp(argv[++i], "sim") == 0);
            continue;
        }
        break;                            /* the rest is the command */
    }

    if (i >= argc) {
        usage(argv[0]);
        return 2;
    }

    if (platform_init() != RES_OK) {
        fprintf(stderr, "platform initialisation failed\n");
        return 1;
    }

    log_set_level(LOG_WARN);

    if (use_sim) {
        if (open_sim(&link) != RES_OK) {
            platform_shutdown();
            return 1;
        }
    } else {
        if (open_uart(&link, port, (uint32_t)baud) != RES_OK) {
            platform_shutdown();
            return 1;
        }
        uart_link_bind(&g_uart, &link);
    }

    if (strcmp(argv[i], "identify") == 0 && i + 1 == argc) {
        rc = do_identify(&link);
    } else if (strcmp(argv[i], "read") == 0 && i + 3 == argc) {
        unsigned long sa = 0, svc = 0;

        if (!parse_num(argv[i + 1], &sa, 0xFF) ||
            !parse_num(argv[i + 2], &svc, TS_SVC_ID_MAX)) {
            fprintf(stderr, "bad arguments to read\n");
            rc = 2;
        } else {
            rc = do_read(&link, (uint8_t)sa, (uint8_t)svc);
        }
    } else if (strcmp(argv[i], "write") == 0 && i + 4 == argc) {
        unsigned long sa = 0, svc = 0, val = 0;

        if (!parse_num(argv[i + 1], &sa, 0xFF) ||
            !parse_num(argv[i + 2], &svc, TS_SVC_ID_MAX) ||
            !parse_num(argv[i + 3], &val, 0xFFFF)) {
            fprintf(stderr, "bad arguments to write\n");
            rc = 2;
        } else {
            rc = do_write(&link, (uint8_t)sa, (uint8_t)svc, (uint16_t)val);
        }
    } else {
        usage(argv[0]);
        rc = 2;
    }

    link_close(&link);
    platform_shutdown();

    return rc;
}
