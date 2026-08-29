/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/config.h"

#include "core/util/strbuf.h"

#include <string.h>

static const char *const k_topic_names[CFG_TOPIC_COUNT] = {
    "topic_telemetry",
    "topic_capabilities",
    "topic_status",
    "topic_control",
    "topic_ack"
};

static const char *const k_topic_defaults[CFG_TOPIC_COUNT] = {
    "{prefix}/from/device/{id}/telemetry",
    "{prefix}/from/device/{id}/capabilities",
    "{prefix}/from/device/{id}/status",
    "{prefix}/to/device/{id}/control",
    "{prefix}/from/device/{id}/control/acknowledgement"
};

const char *config_topic_name(cfg_topic_t t)
{
    if ((int)t < 0 || (int)t >= CFG_TOPIC_COUNT)
        return "topic_unknown";

    return k_topic_names[t];
}

/*
 * Copy, and say whether the whole value fitted.
 *
 * Truncating is the failure this module exists to avoid: a topic template
 * silently cut to its buffer length still renders, still publishes, and goes
 * somewhere nobody is listening. Callers turn a false into a refusal.
 */
static bool set_str(char *dst, size_t cap, const char *v)
{
    size_t n = strlen(v);

    /* Length first, then copy. Copying what fits and reporting failure
     * afterwards leaves the caller with both an error and a half-written
     * setting -- worse than either on its own, because the operator now has a
     * device running a value nobody typed. A refusal changes nothing. */
    if (n >= cap)
        return false;

    memcpy(dst, v, n + 1u);
    return true;
}

void config_defaults(config_t *c)
{
    int i;

    if (!c)
        return;

    memset(c, 0, sizeof(*c));

    /* The host stays empty on purpose: no broker configured means the MQTT
     * service is off, and a unit installed but not yet provisioned never
     * connects to anything by accident. */
    c->mqtt_port        = 1883;
    c->mqtt_keepalive_s = 60;
    c->tls_enable       = false;

    (void)set_str(c->topic_prefix, sizeof(c->topic_prefix), "v1");
    for (i = 0; i < CFG_TOPIC_COUNT; i++)
        (void)set_str(c->topic[i], TOPIC_TEMPLATE_MAX, k_topic_defaults[i]);

    /* A subscriber that joins late needs these to interpret anything else. A
     * retained telemetry sample, by contrast, is a stale sample. */
    c->retain_capabilities = true;
    c->retain_status       = true;

    c->serial_baud           = 9600;
    c->report_interval_s     = 60;
    c->reregister_interval_s = 300;
    c->expand_bitfields      = false;

    (void)set_str(c->log_level, sizeof(c->log_level), "info");
    write_code_map_defaults(&c->write_codes);
}

bool config_mqtt_enabled(const config_t *c)
{
    return c && c->mqtt_host[0] != '\0';
}

/* ------------------------------------------------------------------ helpers */

static void say(char *err, size_t cap, const char *a, const char *b,
                const char *cc)
{
    strbuf_t sb;

    if (!err || cap == 0)
        return;

    sb_init(&sb, err, cap);
    if (a)  sb_puts(&sb, a);
    if (b)  sb_puts(&sb, b);
    if (cc) sb_puts(&sb, cc);
}

static bool is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *trim(char *s)
{
    char *end;

    while (*s && is_space(*s))
        s++;
    if (!*s)
        return s;

    end = s + strlen(s) - 1;
    while (end > s && is_space(*end))
        *end-- = '\0';

    return s;
}

static bool eq_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;

        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return *a == *b;
}

static bool parse_bool(const char *v, bool *out)
{
    if (eq_ci(v, "1") || eq_ci(v, "true") || eq_ci(v, "yes") ||
        eq_ci(v, "on")) {
        *out = true;
        return true;
    }
    if (eq_ci(v, "0") || eq_ci(v, "false") || eq_ci(v, "no") ||
        eq_ci(v, "off")) {
        *out = false;
        return true;
    }
    return false;
}

/* Strict: the whole value must be digits, with an optional sign. strtol would
 * accept "60s" and quietly use 60, which is the kind of near-miss that goes
 * unnoticed for months. */
static bool parse_long(const char *v, long lo, long hi, long *out)
{
    long n = 0;
    bool neg = false;
    size_t i = 0;
    bool any = false;

    if (v[0] == '-') { neg = true; i = 1; }
    else if (v[0] == '+') { i = 1; }

    for (; v[i]; i++) {
        if (v[i] < '0' || v[i] > '9')
            return false;
        n = n * 10 + (v[i] - '0');
        any = true;
        /* Checked after every digit, so the multiply above can never be the
         * thing that overflows. No setting here is anywhere near this large. */
        if (n > 100000000L)
            return false;
    }
    if (!any)
        return false;

    if (neg)
        n = -n;
    if (n < lo || n > hi)
        return false;

    *out = n;
    return true;
}

/* `null` and `none` mean "explicitly unset", which is how the shipped file
 * disables the broker without deleting the key. */
static bool is_null_literal(const char *v)
{
    return v[0] == '\0' || eq_ci(v, "null") || eq_ci(v, "none");
}

static bool set_str_or_empty(char *dst, size_t cap, const char *v)
{
    if (is_null_literal(v)) {
        dst[0] = '\0';
        return true;
    }
    return set_str(dst, cap, v);
}

/* write_result_codes = ok,unknown,read_only,not_taken,no_reply */
static bool parse_code_list(const char *v, write_code_map_t *m)
{
    char        buf[64];
    size_t      i = 0;
    int         n = 0;
    const char *p = v;

    while (*p && n < WRITE_CODE_COUNT) {
        long val = 0;

        i = 0;
        while (*p && *p != ',' && i + 1u < sizeof(buf))
            buf[i++] = *p++;
        buf[i] = '\0';
        if (*p == ',')
            p++;

        if (!parse_long(trim(buf), 0, 65535, &val))
            return false;
        m->code[n++] = (int)val;
    }

    if (n != WRITE_CODE_COUNT || *p != '\0')
        return false;

    m->numeric = true;
    return true;
}

/* ------------------------------------------------------------------- apply */

result_t config_apply_line(config_t *c, const char *line, int lineno,
                           char *err, size_t errcap)
{
    char  buf[CFG_LINE_MAX];
    char *k, *v, *eq, *hash;
    long  n = 0;
    int   i;

    (void)lineno;

    if (!c || !line)
        return RES_EINVAL;
    if (strlen(line) >= sizeof(buf)) {
        say(err, errcap, "line too long", NULL, NULL);
        return RES_EFORMAT;
    }

    (void)set_str(buf, sizeof(buf), line);

    hash = strchr(buf, '#');
    if (hash)
        *hash = '\0';

    k = trim(buf);
    if (!*k)
        return RES_OK;                    /* blank, or comment only */

    eq = strchr(k, '=');
    if (!eq) {
        say(err, errcap, "missing '=' in '", k, "'");
        return RES_EFORMAT;
    }
    *eq = '\0';
    v = trim(eq + 1);
    k = trim(k);

    for (i = 0; i < CFG_TOPIC_COUNT; i++) {
        if (strcmp(k, k_topic_names[i]) == 0) {
            if (is_null_literal(v))
                goto bad_value;
            if (!set_str(c->topic[i], TOPIC_TEMPLATE_MAX, v))
                goto too_long;
            return RES_OK;
        }
    }

    if (strcmp(k, "mqtt_host") == 0) {
        if (!set_str_or_empty(c->mqtt_host, sizeof(c->mqtt_host), v))
            goto too_long;
    } else if (strcmp(k, "mqtt_port") == 0) {
        if (!parse_long(v, 1, 65535, &n)) goto bad_value;
        c->mqtt_port = (uint16_t)n;
    } else if (strcmp(k, "mqtt_username") == 0) {
        if (!set_str_or_empty(c->mqtt_username, sizeof(c->mqtt_username), v))
            goto too_long;
    } else if (strcmp(k, "mqtt_password") == 0) {
        if (!set_str_or_empty(c->mqtt_password, sizeof(c->mqtt_password), v))
            goto too_long;
    } else if (strcmp(k, "mqtt_client_id") == 0) {
        if (!set_str_or_empty(c->mqtt_client_id, sizeof(c->mqtt_client_id), v))
            goto too_long;
    } else if (strcmp(k, "mqtt_keepalive") == 0) {
        if (!parse_long(v, 10, 3600, &n)) goto bad_value;
        c->mqtt_keepalive_s = (uint16_t)n;
    } else if (strcmp(k, "tls_enable") == 0) {
        if (!parse_bool(v, &c->tls_enable)) goto bad_value;
    } else if (strcmp(k, "topic_prefix") == 0) {
        if (is_null_literal(v)) goto bad_value;
        if (!set_str(c->topic_prefix, sizeof(c->topic_prefix), v))
            goto too_long;
    } else if (strcmp(k, "retain_capabilities") == 0) {
        if (!parse_bool(v, &c->retain_capabilities)) goto bad_value;
    } else if (strcmp(k, "retain_status") == 0) {
        if (!parse_bool(v, &c->retain_status)) goto bad_value;
    } else if (strcmp(k, "serial_port") == 0) {
        if (!set_str_or_empty(c->serial_port, sizeof(c->serial_port), v))
            goto too_long;
    } else if (strcmp(k, "serial_baud") == 0) {
        if (!parse_long(v, 1200, 57600, &n)) goto bad_value;
        c->serial_baud = (uint32_t)n;
    } else if (strcmp(k, "device_id") == 0) {
        if (!set_str_or_empty(c->device_id, sizeof(c->device_id), v))
            goto too_long;
    } else if (strcmp(k, "report_interval") == 0) {
        if (!parse_long(v, 5, 86400, &n)) goto bad_value;
        c->report_interval_s = (int)n;
    } else if (strcmp(k, "reregister_interval") == 0) {
        if (!parse_long(v, 10, 86400, &n)) goto bad_value;
        c->reregister_interval_s = (int)n;
    } else if (strcmp(k, "expand_bitfields") == 0) {
        if (!parse_bool(v, &c->expand_bitfields)) goto bad_value;
    } else if (strcmp(k, "log_level") == 0) {
        if (!eq_ci(v, "error") && !eq_ci(v, "warn") &&
            !eq_ci(v, "info")  && !eq_ci(v, "debug")) goto bad_value;
        if (!set_str(c->log_level, sizeof(c->log_level), v))
            goto too_long;
    } else if (strcmp(k, "write_result_codes") == 0) {
        if (is_null_literal(v)) {
            write_code_map_defaults(&c->write_codes);
        } else if (!parse_code_list(v, &c->write_codes)) {
            goto bad_value;
        }
    } else {
        say(err, errcap, "unknown key '", k, "'");
        return RES_ENOTFOUND;
    }

    return RES_OK;

bad_value:
    say(err, errcap, "bad value for '", k, "'");
    return RES_EFORMAT;

too_long:
    say(err, errcap, "value too long for '", k, "'");
    return RES_EFORMAT;
}

/* ---------------------------------------------------------------- validate */

result_t config_validate(const config_t *c, const char *device_id,
                         char *err, size_t errcap)
{
    const char *tmpls[CFG_TOPIC_COUNT];
    size_t      which = 0;
    result_t    r;
    int         i;

    if (!c || !device_id)
        return RES_EINVAL;

    if (!config_mqtt_enabled(c))
        return RES_OK;             /* nothing else can matter */

    if (c->tls_enable) {
        /* Refusing beats connecting in the clear under a configuration that
         * says otherwise. See docs/adr/0008. */
        say(err, errcap, "tls_enable is set but no TLS backend is built in",
            NULL, NULL);
        return RES_EUNSUPPORTED;
    }

    if (device_id[0] == '\0') {
        say(err, errcap, "no device id", NULL, NULL);
        return RES_EINVAL;
    }

    for (i = 0; i < CFG_TOPIC_COUNT; i++)
        tmpls[i] = c->topic[i];

    r = topic_check_set(tmpls, CFG_TOPIC_COUNT, c->topic_prefix, device_id,
                        &which);
    if (r == RES_EINVAL) {
        say(err, errcap, "duplicate topic: ", config_topic_name(
                (cfg_topic_t)which), NULL);
        return RES_EINVAL;
    }
    if (r != RES_OK) {
        say(err, errcap, "unusable topic: ", config_topic_name(
                (cfg_topic_t)which), NULL);
        return r;
    }

    return RES_OK;
}
