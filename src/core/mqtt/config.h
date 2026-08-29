/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Configuration: `key = value` lines, `#` starts a comment.
 *
 * Parsing is a pure function over one line at a time. Reading the file is the
 * application's job, because the core has no file I/O -- which also means the
 * whole of this module is testable without touching a disk.
 *
 * Unknown keys are reported and skipped rather than fatal, so a configuration
 * written for a newer build still starts an older one. Bad *values* are a
 * different matter and are refused: silently keeping a default when an
 * operator plainly asked for something else is how a device ends up running a
 * configuration nobody believes it has.
 */
#ifndef TSB_CORE_MQTT_CONFIG_H
#define TSB_CORE_MQTT_CONFIG_H

#include "core/mqtt/payload.h"
#include "core/mqtt/topic.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFG_STR_MAX     128u
#define CFG_PATH_MAX    256u
/*
 * The prefix is where a deployment scopes itself -- a site, a tenant, a
 * protocol name and a schema version. Thirty-two bytes ran out before the
 * scoping did: "taiseia101/v1" alone is thirteen of them.
 */
#define CFG_PREFIX_MAX   64u
#define CFG_LINE_MAX    512u

typedef enum {
    CFG_TOPIC_TELEMETRY = 0,
    CFG_TOPIC_CAPABILITIES,
    CFG_TOPIC_STATUS,
    CFG_TOPIC_COMMAND,
    CFG_TOPIC_ACK,
    CFG_TOPIC_COUNT
} cfg_topic_t;

typedef struct {
    /* --- broker.  An empty host disables the MQTT service entirely, which is
     * the shipped default: a freshly installed unit never talks to a broker
     * by accident. --- */
    char     mqtt_host[CFG_STR_MAX];
    uint16_t mqtt_port;
    char     mqtt_username[CFG_STR_MAX];   /* empty: use the device id */
    char     mqtt_password[CFG_STR_MAX];
    char     mqtt_client_id[CFG_STR_MAX];  /* empty: use the device id */
    uint16_t mqtt_keepalive_s;

    /* Parsed and validated, but refused at startup: there is no TLS backend
     * built in, and downgrading to cleartext without saying so is worse than
     * not starting. See docs/adr/0008. */
    bool     tls_enable;

    /* --- topics --- */
    char     topic_prefix[CFG_PREFIX_MAX];
    char     topic[CFG_TOPIC_COUNT][TOPIC_TEMPLATE_MAX];
    bool     retain_capabilities;
    bool     retain_status;

    /* --- appliance link --- */
    char     serial_port[CFG_PATH_MAX];
    uint32_t serial_baud;

    /* --- behaviour --- */
    char     device_id[CFG_STR_MAX];
    int      report_interval_s;
    int      reregister_interval_s;
    bool     expand_bitfields;
    char     log_level[16];

    write_code_map_t write_codes;
} config_t;

void config_defaults(config_t *c);

/**
 * Apply one line.
 *
 * RES_OK for a blank line, a comment, or a key that was applied.
 * RES_ENOTFOUND for an unknown key -- reported, not fatal.
 * RES_EFORMAT for a malformed line or an unusable value.
 *
 * `err` receives a message on anything but RES_OK; pass NULL to skip it.
 */
result_t config_apply_line(config_t *c, const char *line, int lineno,
                           char *err, size_t errcap);

/**
 * Reject combinations that cannot work, before anything starts.
 *
 * `device_id` is needed because the topic set is rendered here: a topic that
 * does not fit, or two topics that collide, are startup failures rather than
 * something to discover once per reporting interval.
 */
result_t config_validate(const config_t *c, const char *device_id,
                         char *err, size_t errcap);

bool config_mqtt_enabled(const config_t *c);

const char *config_topic_name(cfg_topic_t t);

#endif /* TSB_CORE_MQTT_CONFIG_H */
