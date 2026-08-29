/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

#include "core/mqtt/config.h"

static void apply(config_t *c, const char *line, result_t want)
{
    char err[128];

    err[0] = '\0';
    CHECK_EQ(config_apply_line(c, line, 1, err, sizeof(err)), want);
    if (want != RES_OK)
        CHECK(err[0] != '\0');      /* a refusal always says why */
}

/*
 * The shipped default is a device that talks to nobody. A unit installed but
 * not yet provisioned should not connect to a broker by accident.
 */
static void the_default_is_a_disabled_service(void)
{
    config_t c;

    config_defaults(&c);
    CHECK(!config_mqtt_enabled(&c));
    CHECK_EQ(c.mqtt_port, 1883);
    CHECK_EQ(c.mqtt_keepalive_s, 60);
    CHECK_EQ(c.tls_enable, 0);
    CHECK_EQ(c.serial_baud, 9600);
    CHECK_EQ(c.report_interval_s, 60);
    CHECK_STREQ(c.topic_prefix, "taiseia101/v1");
    CHECK_STREQ(c.log_level, "info");
    CHECK_STREQ(c.topic[CFG_TOPIC_TELEMETRY],
                "{prefix}/from/appliance/{id}/telemetry");

    /* Late subscribers need these to interpret anything else; a retained
     * telemetry sample would just be a stale one. */
    CHECK_EQ(c.retain_capabilities, 1);
    CHECK_EQ(c.retain_status, 1);
    CHECK_EQ(c.write_codes.numeric, 0);
}

static void keys_are_applied(void)
{
    config_t c;

    config_defaults(&c);

    apply(&c, "mqtt_host = broker.example", RES_OK);
    apply(&c, "  mqtt_port=8883  ", RES_OK);
    apply(&c, "mqtt_username = dev-1", RES_OK);
    apply(&c, "mqtt_keepalive = 30", RES_OK);
    apply(&c, "tls_enable = no", RES_OK);
    apply(&c, "topic_prefix = v2", RES_OK);
    apply(&c, "topic_status = {prefix}/dev/{id}/alive", RES_OK);
    apply(&c, "retain_status = false", RES_OK);
    apply(&c, "serial_port = /dev/ttyUSB0", RES_OK);
    apply(&c, "serial_baud = 19200", RES_OK);
    apply(&c, "device_id = TS-9", RES_OK);
    apply(&c, "report_interval = 120", RES_OK);
    apply(&c, "log_level = DEBUG", RES_OK);

    CHECK(config_mqtt_enabled(&c));
    CHECK_STREQ(c.mqtt_host, "broker.example");
    CHECK_EQ(c.mqtt_port, 8883);
    CHECK_STREQ(c.mqtt_username, "dev-1");
    CHECK_EQ(c.mqtt_keepalive_s, 30);
    CHECK_STREQ(c.topic_prefix, "v2");
    CHECK_STREQ(c.topic[CFG_TOPIC_STATUS], "{prefix}/dev/{id}/alive");
    CHECK_EQ(c.retain_status, 0);
    CHECK_STREQ(c.serial_port, "/dev/ttyUSB0");
    CHECK_EQ(c.serial_baud, 19200);
    CHECK_STREQ(c.device_id, "TS-9");
    CHECK_EQ(c.report_interval_s, 120);
    CHECK_STREQ(c.log_level, "DEBUG");
}

static void blanks_comments_and_null_literals(void)
{
    config_t c;

    config_defaults(&c);

    apply(&c, "", RES_OK);
    apply(&c, "   ", RES_OK);
    apply(&c, "# a comment", RES_OK);
    apply(&c, "mqtt_host = broker  # trailing comment", RES_OK);
    CHECK_STREQ(c.mqtt_host, "broker");

    /* null and none turn a key off without deleting it from the file. */
    apply(&c, "mqtt_host = null", RES_OK);
    CHECK(!config_mqtt_enabled(&c));
    apply(&c, "mqtt_host = broker", RES_OK);
    apply(&c, "mqtt_host = NONE", RES_OK);
    CHECK(!config_mqtt_enabled(&c));
    apply(&c, "mqtt_username =", RES_OK);
    CHECK_STREQ(c.mqtt_username, "");
}

/*
 * An unknown key is reported and skipped, so a file written for a newer build
 * still starts an older one. A bad value is refused: silently keeping the
 * default when an operator plainly asked for something else is how a device
 * ends up running a configuration nobody believes it has.
 */
static void unknown_keys_are_skipped_and_bad_values_are_not(void)
{
    config_t c;

    config_defaults(&c);

    apply(&c, "some_future_key = 1", RES_ENOTFOUND);
    apply(&c, "no equals sign here", RES_EFORMAT);

    apply(&c, "mqtt_port = 0", RES_EFORMAT);
    apply(&c, "mqtt_port = 70000", RES_EFORMAT);
    apply(&c, "mqtt_port = 1883x", RES_EFORMAT);
    apply(&c, "mqtt_port = ", RES_EFORMAT);
    CHECK_EQ(c.mqtt_port, 1883);          /* untouched by any of them */

    apply(&c, "mqtt_keepalive = 5", RES_EFORMAT);     /* below the floor */
    apply(&c, "tls_enable = maybe", RES_EFORMAT);
    apply(&c, "serial_baud = 115200", RES_EFORMAT);   /* above the ceiling */
    apply(&c, "report_interval = 1", RES_EFORMAT);
    apply(&c, "log_level = shout", RES_EFORMAT);
    apply(&c, "topic_prefix = null", RES_EFORMAT);
    apply(&c, "topic_telemetry = none", RES_EFORMAT);

    CHECK_EQ(config_apply_line(NULL, "a = b", 1, NULL, 0), RES_EINVAL);
    CHECK_EQ(config_apply_line(&c, NULL, 1, NULL, 0), RES_EINVAL);
}

static void write_result_codes_are_configurable(void)
{
    config_t c;

    config_defaults(&c);
    CHECK_EQ(c.write_codes.numeric, 0);

    apply(&c, "write_result_codes = 0,255,255,240,255", RES_OK);
    CHECK_EQ(c.write_codes.numeric, 1);
    CHECK_EQ(c.write_codes.code[APPL_WRITE_OK], 0);
    CHECK_EQ(c.write_codes.code[APPL_WRITE_UNKNOWN], 255);
    CHECK_EQ(c.write_codes.code[APPL_WRITE_NOT_TAKEN], 240);

    /* Back to names. */
    apply(&c, "write_result_codes = none", RES_OK);
    CHECK_EQ(c.write_codes.numeric, 0);

    apply(&c, "write_result_codes = 0,1,2", RES_EFORMAT);
    apply(&c, "write_result_codes = 0,1,2,3,4,5", RES_EFORMAT);
    apply(&c, "write_result_codes = 0,1,x,3,4", RES_EFORMAT);
}

static void validation_runs_before_anything_starts(void)
{
    config_t c;
    char     err[128];

    /* A disabled service has nothing to validate. */
    config_defaults(&c);
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_OK);

    /* Refusing beats connecting in the clear under a config that says
     * otherwise. */
    config_defaults(&c);
    apply(&c, "mqtt_host = broker", RES_OK);
    apply(&c, "tls_enable = true", RES_OK);
    err[0] = '\0';
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_EUNSUPPORTED);
    CHECK(err[0] != '\0');

    config_defaults(&c);
    apply(&c, "mqtt_host = broker", RES_OK);
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_OK);
    CHECK_EQ(config_validate(&c, "", err, sizeof(err)), RES_EINVAL);

    /* Two topics rendering the same string is a configuration mistake that
     * looks like data corruption downstream. */
    apply(&c, "topic_status = {prefix}/from/appliance/{id}/telemetry",
          RES_OK);
    err[0] = '\0';
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_EINVAL);
    CHECK(err[0] != '\0');

    /* And so is a topic that fits as a template but not once rendered. */
    config_defaults(&c);
    apply(&c, "mqtt_host = broker", RES_OK);
    apply(&c, "topic_telemetry = {id}/{id}/{id}/{id}/{id}", RES_OK);
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_OK);
    err[0] = '\0';
    CHECK_EQ(config_validate(&c,
                             "0123456789012345678901234567890123456789",
                             err, sizeof(err)), RES_ETOOBIG);
    /*
     * Too long and malformed have different cures, so they must not share a
     * message. An operator told only "unusable" has nothing to act on.
     */
    CHECK(strstr(err, "too long") != NULL);
    CHECK(strstr(err, "device id") != NULL);

    config_defaults(&c);
    apply(&c, "mqtt_host = broker", RES_OK);
    apply(&c, "topic_telemetry = {prefix}/{nope}/x", RES_OK);
    err[0] = '\0';
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_EFORMAT);
    CHECK(strstr(err, "too long") == NULL);
    CHECK(strstr(err, "template") != NULL);

    /*
     * The prefix is where a deployment scopes itself -- a site, a tenant, the
     * protocol name and the schema version. Thirty-two bytes ran out before
     * the scoping did.
     */
    config_defaults(&c);
    apply(&c, "mqtt_host = broker", RES_OK);
    apply(&c, "topic_prefix = acme-corp/building-3/floor-2/taiseia101/v1",
          RES_OK);
    CHECK_STREQ(c.topic_prefix, "acme-corp/building-3/floor-2/taiseia101/v1");
    CHECK_EQ(config_validate(&c, "TS-1", err, sizeof(err)), RES_OK);

    /*
     * A value longer than its field is refused outright. Truncating it would
     * leave the device running a topic the operator never wrote, and it would
     * still render, still publish, and go nowhere useful.
     */
    config_defaults(&c);
    apply(&c, "topic_telemetry = {prefix}/{id}/aaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          RES_EFORMAT);
    CHECK_STREQ(c.topic[CFG_TOPIC_TELEMETRY],
                "{prefix}/from/appliance/{id}/telemetry");
    apply(&c, "mqtt_host = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          RES_EFORMAT);
    CHECK(!config_mqtt_enabled(&c));

    CHECK_EQ(config_validate(NULL, "TS-1", err, sizeof(err)), RES_EINVAL);
    CHECK_EQ(config_validate(&c, NULL, err, sizeof(err)), RES_EINVAL);

    CHECK_STREQ(config_topic_name(CFG_TOPIC_ACK), "topic_ack");
    CHECK_STREQ(config_topic_name((cfg_topic_t)99), "topic_unknown");
}

void test_config(void)
{
    the_default_is_a_disabled_service();
    keys_are_applied();
    blanks_comments_and_null_literals();
    unknown_keys_are_skipped_and_bad_values_are_not();
    write_result_codes_are_configurable();
    validation_runs_before_anything_starts();
}
