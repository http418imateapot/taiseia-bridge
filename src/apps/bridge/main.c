/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The bridge.
 *
 * This file is composition, not mechanism: it owns an appliance model, an MQTT
 * client and a clock, and it hands each of them the ports the platform layer
 * provides. Everything it does is tested elsewhere, which is the point --
 * main() should be the least interesting file in the project.
 *
 * The loop never blocks. Each pass advances the appliance link one step,
 * advances the MQTT client one step, and does whatever the schedule says is
 * due. A slow appliance cannot stall the broker connection and a slow broker
 * cannot stall the appliance.
 */
#include "adapters/platform.h"
#include "adapters/sim/sim_link.h"
#include "adapters/taiseia_uart/uart_link.h"
#include "core/model/appliance.h"
#include "core/mqtt/client.h"
#include "core/mqtt/config.h"
#include "core/mqtt/control.h"
#include "core/mqtt/payload.h"
#include "core/mqtt/topic.h"
#include "core/util/strbuf.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOOP_IDLE_MS 5

typedef enum { LINK_UART = 0, LINK_SIM } link_kind_t;

typedef struct {
    config_t          cfg;
    appliance_t       appl;
    mqtt_client_t     mqtt;

    link_kind_t       kind;
    platform_serial_t pser;
    serial_t          serial;
    uart_link_t       uart;
    sim_link_t        sim;
    link_t            link;

    platform_socket_t psock;
    socket_t          sock;

    char              topic[CFG_TOPIC_COUNT][TOPIC_MAX];
    char              will[128];

    /* One command at a time; a second is refused rather than queued. */
    bool              cmd_pending;
    control_cmd_t     cmd;
    bool              reject_pending;
    control_cmd_t     reject;

    int64_t           next_report_ms;
    bool              was_online;
    bool              status_sent;
    unsigned          last_sweeps;

    char              buf[MQTT_PAYLOAD_MAX];
    strbuf_t          sb;
} bridge_t;

/* Large enough that it belongs in the data segment rather than on a stack. */
static bridge_t g_b;

/* ------------------------------------------------------------------- usage */

static void usage(const char *prog)
{
    printf("taiseia-bridge %s\n\n", TSB_VERSION);
    printf("usage: %s [options]\n\n", prog);
    printf("  -c, --config PATH    configuration file\n");
    printf("  -l, --log-level LVL  error | warn | info | debug\n");
    printf("      --link KIND      uart (default) or sim\n");
    printf("      --run-for SECS   stop after this long; 0 runs forever\n");
    printf("  -h, --help\n");
    printf("  -v, --version\n\n");
    printf("With no broker configured the payloads are written to the log\n");
    printf("instead of published, which is what --link sim is for.\n");
}

/* --------------------------------------------------------- config from disk */

/*
 * Reading the file is the application's job: the core has no file I/O, which
 * is what keeps every line of the parser testable without a disk.
 */
static result_t load_config(config_t *cfg, const char *path)
{
    FILE *fp;
    char  line[CFG_LINE_MAX];
    int   lineno = 0;
    int   bad = 0;

    fp = fopen(path, "r");
    if (!fp) {
        LOG_E("cannot open %s", path);
        return RES_ENOTFOUND;
    }

    while (fgets(line, sizeof(line), fp)) {
        char     err[128];
        result_t r;

        lineno++;
        err[0] = '\0';
        r = config_apply_line(cfg, line, lineno, err, sizeof(err));

        if (r == RES_ENOTFOUND) {
            /* A file written for a newer build still starts an older one. */
            LOG_W("%s:%d: %s (ignored)", path, lineno, err);
        } else if (r != RES_OK) {
            LOG_E("%s:%d: %s", path, lineno, err);
            bad++;
        }
    }
    fclose(fp);

    if (bad > 0) {
        /* Starting with a configuration that was partly rejected means running
         * something nobody wrote down. */
        LOG_E("%d bad line(s) in %s; refusing to start", bad, path);
        return RES_EFORMAT;
    }

    LOG_I("loaded %s (%d lines)", path, lineno);
    return RES_OK;
}

/* ------------------------------------------------------------------- links */

static const uint8_t k_sim_bits[]  = { 0x00, 0x03 };
static const uint8_t k_sim_range[] = { 0x00, 0x64 };

/* A dehumidifier, roughly: a mode, a target, and a reading. Enough to see the
 * whole path work without any hardware. */
static result_t start_sim(bridge_t *b)
{
    sim_link_init(&b->sim, 0x04);
    if (sim_link_set_identity(&b->sim, TS_FORMAT_GENERAL, TS_CLASS_APPLIANCE,
                              0x0004, "SIMULATOR", "SIM-0001") != RES_OK)
        return RES_EINVAL;

    if (sim_link_add_service(&b->sim, 0x00, 0, true,  1,  k_sim_bits,  2)
            != RES_OK ||
        sim_link_add_service(&b->sim, 0x03, 0, true,  55, k_sim_range, 2)
            != RES_OK ||
        sim_link_add_service(&b->sim, 0x07, 0, false, 62, k_sim_range, 2)
            != RES_OK)
        return RES_EINVAL;

    sim_link_bind(&b->sim, &b->link);
    LOG_I("appliance link: simulator");

    return RES_OK;
}

static result_t start_uart(bridge_t *b)
{
    serial_params_t p;
    result_t        r;

    if (b->cfg.serial_port[0] == '\0') {
        LOG_E("no serial_port configured");
        return RES_EINVAL;
    }

    serial_params_defaults(&p);
    p.baud = b->cfg.serial_baud;

    platform_serial_bind(&b->pser, &b->serial);

    r = serial_open(&b->serial, b->cfg.serial_port, &p);
    if (r != RES_OK) {
        LOG_E("cannot open %s: %s", b->cfg.serial_port, res_str(r));
        return r;
    }

    r = uart_link_init(&b->uart, &b->serial, platform_now_ms);
    if (r != RES_OK)
        return r;

    uart_link_bind(&b->uart, &b->link);
    LOG_I("appliance link: %s at %u baud", b->cfg.serial_port,
          (unsigned)b->cfg.serial_baud);

    return RES_OK;
}

/* ------------------------------------------------------------- publication */

static void emit(bridge_t *b, cfg_topic_t which, bool retain)
{
    result_t r;

    if (!config_mqtt_enabled(&b->cfg)) {
        /* No broker configured: the payloads go to the log, which is what
         * makes a simulator run worth watching. */
        LOG_I("%s %s", b->topic[which], sb_str(&b->sb));
        return;
    }

    if (!mqttc_ready(&b->mqtt)) {
        /* Not connected is the normal state during an outage, and saying so
         * once per report at warning level would bury everything else. */
        LOG_D("%s dropped: not connected", b->topic[which]);
        return;
    }

    r = mqttc_publish(&b->mqtt, b->topic[which], sb_str(&b->sb),
                      sb_len(&b->sb), retain);
    if (r == RES_EBUSY)
        LOG_W("%s dropped: the send buffer is still draining",
              b->topic[which]);
    else if (r != RES_OK)
        LOG_W("%s not published: %s", b->topic[which], res_str(r));
}

static bool stamp_now(char *out, size_t cap)
{
    return platform_iso8601_utc(out, cap);
}

static void publish_status(bridge_t *b, bool online)
{
    char stamp[PLATFORM_TIMESTAMP_MAX];

    if (!stamp_now(stamp, sizeof(stamp)))
        stamp[0] = '\0';

    if (payload_status(&b->sb, b->cfg.device_id,
                       stamp[0] ? stamp : NULL, online) == RES_OK)
        emit(b, CFG_TOPIC_STATUS, b->cfg.retain_status);
}

static void publish_reports(bridge_t *b)
{
    char stamp[PLATFORM_TIMESTAMP_MAX];

    if (!stamp_now(stamp, sizeof(stamp)))
        stamp[0] = '\0';

    if (payload_telemetry(&b->sb, b->cfg.device_id,
                          stamp[0] ? stamp : NULL, &b->appl) == RES_OK)
        emit(b, CFG_TOPIC_TELEMETRY, false);
    else
        LOG_W("telemetry did not fit");

    if (payload_capabilities(&b->sb, b->cfg.device_id,
                             stamp[0] ? stamp : NULL, &b->appl) == RES_OK)
        emit(b, CFG_TOPIC_CAPABILITIES, b->cfg.retain_capabilities);
    else
        LOG_W("capabilities did not fit");
}

static void publish_ack(bridge_t *b, const control_cmd_t *cmd,
                        const appl_write_batch_t *batch)
{
    char stamp[PLATFORM_TIMESTAMP_MAX];

    if (!stamp_now(stamp, sizeof(stamp)))
        stamp[0] = '\0';

    if (payload_ack(&b->sb, b->cfg.device_id, stamp[0] ? stamp : NULL,
                    cmd->has_tag ? cmd->tag : NULL, cmd->tag_len,
                    batch, &b->cfg.write_codes) == RES_OK)
        emit(b, CFG_TOPIC_ACK, false);
}

static void publish_ack_error(bridge_t *b, const control_cmd_t *cmd,
                              const char *why)
{
    char stamp[PLATFORM_TIMESTAMP_MAX];

    if (!stamp_now(stamp, sizeof(stamp)))
        stamp[0] = '\0';

    if (payload_ack_error(&b->sb, b->cfg.device_id, stamp[0] ? stamp : NULL,
                          (cmd && cmd->has_tag) ? cmd->tag : NULL,
                          cmd ? cmd->tag_len : 0, why) == RES_OK)
        emit(b, CFG_TOPIC_ACK, false);
}

/* ------------------------------------------------------------ control path */

static void on_message(void *ctx, const char *topic, size_t topic_len,
                       const uint8_t *payload, size_t len)
{
    bridge_t *b = (bridge_t *)ctx;
    result_t  r;

    (void)topic;
    (void)topic_len;

    /* Parsing happens here; acting on it happens in the loop, so that the
     * client is never re-entered from inside its own callback. */
    if (b->cmd_pending || b->reject_pending) {
        control_cmd_t tmp;

        if (control_parse((const char *)payload, len, &tmp) == RES_OK) {
            b->reject         = tmp;
            b->reject_pending = true;
        }
        LOG_W("command refused: one is already in flight");
        return;
    }

    r = control_parse((const char *)payload, len, &b->cmd);
    if (r != RES_OK) {
        LOG_W("malformed command: %s", res_str(r));
        /* control_parse fills the tag before it reaches the part that failed,
         * so an error acknowledgement can still be correlated. */
        b->reject         = b->cmd;
        b->reject_pending = true;
        memset(&b->cmd, 0, sizeof(b->cmd));
        return;
    }

    b->cmd_pending = true;
}

static void service_control(bridge_t *b)
{
    if (b->reject_pending && !appl_write_active(&b->appl)) {
        publish_ack_error(b, &b->reject, "busy or malformed");
        b->reject_pending = false;
    }

    if (b->cmd_pending && !appl_write_active(&b->appl)) {
        result_t r = appl_write_begin(&b->appl, b->cmd.items, b->cmd.n);

        if (r != RES_OK) {
            publish_ack_error(b, &b->cmd, res_str(r));
            b->cmd_pending = false;
        }
        return;
    }

    if (appl_write_active(&b->appl) && appl_write_done(&b->appl)) {
        const appl_write_batch_t *batch = appl_write_batch(&b->appl);

        if (batch)
            publish_ack(b, &b->cmd, batch);
        appl_write_clear(&b->appl);
        b->cmd_pending = false;
    }
}

/* -------------------------------------------------------------------- main */

static bool arg_is(const char *a, const char *shrt, const char *lng)
{
    return (shrt && strcmp(a, shrt) == 0) || (lng && strcmp(a, lng) == 0);
}

int main(int argc, char *argv[])
{
    bridge_t   *b = &g_b;
    const char *cfg_path = NULL;
    const char *level = NULL;
    long        run_for = 0;
    int64_t     started;
    int         i;
    char        err[128];

    config_defaults(&b->cfg);
    b->kind = LINK_UART;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (arg_is(a, "-h", "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (arg_is(a, "-v", "--version")) {
            printf("%s\n", TSB_VERSION);
            return 0;
        }
        if (arg_is(a, "-c", "--config") && i + 1 < argc) {
            cfg_path = argv[++i];
            continue;
        }
        if (arg_is(a, "-l", "--log-level") && i + 1 < argc) {
            level = argv[++i];
            continue;
        }
        if (arg_is(a, NULL, "--link") && i + 1 < argc) {
            const char *k = argv[++i];

            if (strcmp(k, "sim") == 0) {
                b->kind = LINK_SIM;
            } else if (strcmp(k, "uart") == 0) {
                b->kind = LINK_UART;
            } else {
                fprintf(stderr, "unknown link '%s'\n", k);
                return 2;
            }
            continue;
        }
        if (arg_is(a, NULL, "--run-for") && i + 1 < argc) {
            run_for = strtol(argv[++i], NULL, 10);
            continue;
        }

        fprintf(stderr, "unexpected argument '%s'\n", a);
        usage(argv[0]);
        return 2;
    }

    if (platform_init() != RES_OK) {
        fprintf(stderr, "platform initialisation failed\n");
        return 1;
    }
    platform_install_stop_handler();

    if (cfg_path && load_config(&b->cfg, cfg_path) != RES_OK) {
        platform_shutdown();
        return 1;
    }

    /* The command line wins over the file, so a level can be raised for one
     * run without editing anything. */
    log_set_level(log_level_from_str(level ? level : b->cfg.log_level));

    if (b->cfg.device_id[0] == '\0') {
        LOG_E("no device_id configured");
        platform_shutdown();
        return 1;
    }

    err[0] = '\0';
    if (config_validate(&b->cfg, b->cfg.device_id, err, sizeof(err))
            != RES_OK) {
        LOG_E("configuration: %s", err);
        platform_shutdown();
        return 1;
    }

    for (i = 0; i < CFG_TOPIC_COUNT; i++) {
        if (topic_render(b->topic[i], TOPIC_MAX, b->cfg.topic[i],
                         b->cfg.topic_prefix, b->cfg.device_id) != RES_OK) {
            LOG_E("cannot render %s", config_topic_name((cfg_topic_t)i));
            platform_shutdown();
            return 1;
        }
    }

    sb_init(&b->sb, b->buf, sizeof(b->buf));

    if ((b->kind == LINK_SIM ? start_sim(b) : start_uart(b)) != RES_OK) {
        platform_shutdown();
        return 1;
    }

    {
        appl_policy_t p;

        appl_policy_defaults(&p);
        p.sweep_interval_ms      = b->cfg.report_interval_s * 1000;
        p.reregister_interval_ms = b->cfg.reregister_interval_s * 1000;
        appl_init(&b->appl, &p);
    }

    if (config_mqtt_enabled(&b->cfg)) {
        mqttc_options_t o;

        /* The will is what the broker says on our behalf if we vanish without
         * a word, so it has to be built before we connect. */
        sb_init(&b->sb, b->will, sizeof(b->will));
        (void)payload_status(&b->sb, b->cfg.device_id, NULL, false);
        sb_init(&b->sb, b->buf, sizeof(b->buf));

        memset(&o, 0, sizeof(o));
        o.host            = b->cfg.mqtt_host;
        o.port            = b->cfg.mqtt_port;
        o.client_id       = b->cfg.mqtt_client_id[0] ? b->cfg.mqtt_client_id
                                                     : b->cfg.device_id;
        o.username        = b->cfg.mqtt_username[0] ? b->cfg.mqtt_username
                                                    : NULL;
        o.password        = b->cfg.mqtt_password[0] ? b->cfg.mqtt_password
                                                    : NULL;
        o.keepalive_s     = b->cfg.mqtt_keepalive_s;
        o.will_topic      = b->topic[CFG_TOPIC_STATUS];
        o.will_payload    = b->will;
        o.will_len        = strlen(b->will);
        o.will_retain     = b->cfg.retain_status;
        o.subscribe_topic = b->topic[CFG_TOPIC_CONTROL];
        o.on_message      = on_message;
        o.ctx             = b;

        platform_socket_bind(&b->psock, &b->sock);
        mqttc_init(&b->mqtt, &o);

        LOG_I("broker %s:%u as %s", b->cfg.mqtt_host,
              (unsigned)b->cfg.mqtt_port, o.client_id);
    } else {
        LOG_W("no broker configured: payloads go to the log");
    }

    LOG_I("taiseia-bridge %s starting as %s", TSB_VERSION, b->cfg.device_id);

    started            = platform_now_ms();
    b->next_report_ms  = started;
    b->was_online      = false;

    while (!platform_stop_requested()) {
        int64_t now = platform_now_ms();

        (void)appl_tick(&b->appl, &b->link, now);

        if (config_mqtt_enabled(&b->cfg))
            (void)mqttc_tick(&b->mqtt, &b->sock, now);

        service_control(b);

        {
            bool online = appl_online(&b->appl);

            if (online != b->was_online || !b->status_sent) {
                publish_status(b, online);
                b->was_online  = online;
                b->status_sent = true;
            }
        }

        {
            unsigned sweeps = appl_sweeps(&b->appl);

            /*
             * Report when a sweep finishes, or when the interval comes round
             * with nothing having finished -- an appliance that is offline
             * still gets reported, but one that has just come online is not
             * reported before anything has been read from it.
             */
            if (sweeps != b->last_sweeps || now >= b->next_report_ms) {
                publish_reports(b);
                b->last_sweeps    = sweeps;
                b->next_report_ms = now + b->cfg.report_interval_s * 1000;
            }
        }

        if (run_for > 0 && now - started >= run_for * 1000)
            break;

        platform_sleep_ms(LOOP_IDLE_MS);
    }

    LOG_I("stopping");

    /* A last word on the status topic, so a consumer is not left to infer our
     * absence from the will. */
    publish_status(b, false);
    if (config_mqtt_enabled(&b->cfg)) {
        (void)mqttc_tick(&b->mqtt, &b->sock, platform_now_ms());
        sock_close(&b->sock);
    }
    link_close(&b->link);

    platform_shutdown();
    return 0;
}
