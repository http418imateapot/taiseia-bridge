/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/client.h"

#include <string.h>

void mqttc_init(mqtt_client_t *c, const mqttc_options_t *opt)
{
    if (!c)
        return;

    memset(c, 0, sizeof(*c));

    if (opt)
        c->opt = *opt;

    if (c->opt.port == 0)
        c->opt.port = 1883;
    if (c->opt.keepalive_s == 0)
        c->opt.keepalive_s = 60;
    if (c->opt.reconnect_min_ms <= 0)
        c->opt.reconnect_min_ms = 1000;
    if (c->opt.reconnect_max_ms < c->opt.reconnect_min_ms)
        c->opt.reconnect_max_ms = c->opt.reconnect_min_ms;

    c->state          = MQTTC_DOWN;
    c->backoff_ms     = c->opt.reconnect_min_ms;
    c->next_packet_id = 1;
}

mqttc_state_t mqttc_state(const mqtt_client_t *c)
{
    return c ? c->state : MQTTC_DOWN;
}

bool mqttc_ready(const mqtt_client_t *c)
{
    return c && c->state == MQTTC_READY;
}

uint8_t mqttc_last_connack(const mqtt_client_t *c)
{
    return c ? c->last_connack : 0;
}

const char *mqttc_state_str(mqttc_state_t s)
{
    switch (s) {
    case MQTTC_DOWN:          return "down";
    case MQTTC_CONNECTING:    return "connecting";
    case MQTTC_AWAIT_CONNACK: return "awaiting connack";
    case MQTTC_AWAIT_SUBACK:  return "awaiting suback";
    case MQTTC_READY:         return "ready";
    }
    return "unknown";
}

/* --------------------------------------------------------------- plumbing */

static void go_down(mqtt_client_t *c, socket_t *sock, int64_t now_ms)
{
    sock_close(sock);

    c->state        = MQTTC_DOWN;
    c->rx_len       = 0;
    c->tx_len       = 0;
    c->tx_sent      = 0;
    c->ping_sent_ms = 0;

    c->next_attempt_ms = now_ms + c->backoff_ms;

    c->backoff_ms *= 2;
    if (c->backoff_ms > c->opt.reconnect_max_ms)
        c->backoff_ms = c->opt.reconnect_max_ms;
}

/* Room for one more packet, after discarding what has already gone out. */
static size_t tx_room(mqtt_client_t *c)
{
    if (c->tx_sent > 0 && c->tx_sent == c->tx_len) {
        c->tx_len  = 0;
        c->tx_sent = 0;
    } else if (c->tx_sent > 0) {
        memmove(c->tx, c->tx + c->tx_sent, c->tx_len - c->tx_sent);
        c->tx_len -= c->tx_sent;
        c->tx_sent = 0;
    }

    return sizeof(c->tx) - c->tx_len;
}

static result_t flush_tx(mqtt_client_t *c, socket_t *sock, int64_t now_ms)
{
    while (c->tx_sent < c->tx_len) {
        size_t   sent = 0;
        result_t r = sock_send(sock, c->tx + c->tx_sent,
                               c->tx_len - c->tx_sent, &sent);

        if (r == RES_EAGAIN)
            return RES_OK;              /* try again next tick */
        if (r != RES_OK)
            return r;
        if (sent == 0)
            return RES_OK;

        c->tx_sent += sent;
        c->last_tx_ms = now_ms;
    }

    (void)tx_room(c);
    return RES_OK;
}

/* ---------------------------------------------------------------- inbound */

static result_t handle_packet(mqtt_client_t *c, const mqtt_packet_t *p,
                              int64_t now_ms)
{
    switch (c->state) {
    case MQTTC_AWAIT_CONNACK: {
        bool     present = false;
        uint8_t  code = 0;
        result_t r;

        if (p->type != MQTT_CONNACK)
            return RES_EFORMAT;

        r = mqtt_connack(p, &present, &code);
        if (r != RES_OK)
            return r;

        c->last_connack = code;
        if (code != MQTT_CONNACK_ACCEPTED) {
            /* Bad credentials are not a transport problem and retrying
             * faster will not fix them, so the ordinary backoff applies. */
            return RES_EIO;
        }

        /* The connection is up, so the next failure starts from the shortest
         * backoff rather than from whatever the last outage grew it to. */
        c->backoff_ms = c->opt.reconnect_min_ms;

        if (c->opt.subscribe_topic && c->opt.subscribe_topic[0]) {
            size_t len = 0;

            if (tx_room(c) < MQTT_PACKET_MAX)
                return RES_ETOOBIG;

            c->sub_packet_id = c->next_packet_id++;
            if (c->next_packet_id == 0)
                c->next_packet_id = 1;

            r = mqtt_encode_subscribe(c->tx + c->tx_len,
                                      sizeof(c->tx) - c->tx_len, &len,
                                      c->sub_packet_id,
                                      c->opt.subscribe_topic);
            if (r != RES_OK)
                return r;

            c->tx_len += len;
            c->state       = MQTTC_AWAIT_SUBACK;
            c->deadline_ms = now_ms + MQTTC_ACK_TIMEOUT_MS;
            return RES_OK;
        }

        c->state = MQTTC_READY;
        return RES_OK;
    }

    case MQTTC_AWAIT_SUBACK: {
        uint16_t id = 0;
        uint8_t  code = 0;
        result_t r;

        if (p->type != MQTT_SUBACK)
            return RES_EFORMAT;

        r = mqtt_suback(p, &id, &code);
        if (r != RES_OK)
            return r;
        if (id != c->sub_packet_id)
            return RES_EFORMAT;
        if (code == MQTT_SUBACK_FAILURE) {
            /* Without the control topic the bridge is publish-only, which is
             * not what was asked for. Start again rather than run degraded
             * and silent about it. */
            return RES_EIO;
        }

        c->state = MQTTC_READY;
        return RES_OK;
    }

    case MQTTC_READY:
        if (p->type == MQTT_PINGRESP) {
            c->ping_sent_ms = 0;
            return RES_OK;
        }
        if (p->type == MQTT_PUBLISH) {
            const char    *topic = NULL;
            size_t         tlen = 0;
            const uint8_t *payload = NULL;
            size_t         plen = 0;
            result_t       r = mqtt_publish_parts(p, &topic, &tlen, &payload,
                                                  &plen);

            if (r != RES_OK) {
                /* A message we cannot read is not a reason to drop the
                 * connection: the next one may be fine, and the sender is
                 * not us. */
                return RES_OK;
            }
            if (c->opt.on_message)
                c->opt.on_message(c->opt.ctx, topic, tlen, payload, plen);
            return RES_OK;
        }
        /* SUBACK or CONNACK here means the broker and we disagree about where
         * we are. */
        return RES_EFORMAT;

    case MQTTC_DOWN:
    case MQTTC_CONNECTING:
    default:
        return RES_EFORMAT;
    }
}

static result_t pump_rx(mqtt_client_t *c, socket_t *sock, int64_t now_ms)
{
    for (;;) {
        size_t   need = 0;
        size_t   got = 0;
        result_t r;

        /* Take whatever is waiting first, so one tick can finish a packet
         * that arrived in two pieces. */
        if (c->rx_len < sizeof(c->rx)) {
            r = sock_recv(sock, c->rx + c->rx_len, sizeof(c->rx) - c->rx_len,
                          &got);
            if (r == RES_OK)
                c->rx_len += got;
            else if (r != RES_EAGAIN)
                return r;
        }

        r = mqtt_packet_len(c->rx, c->rx_len, &need);
        if (r == RES_EAGAIN) {
            if (got == 0)
                return RES_OK;          /* nothing more to be had this tick */
            continue;
        }
        if (r != RES_OK)
            return r;                   /* malformed, or beyond what we hold */

        if (need > c->rx_len) {
            if (need > sizeof(c->rx))
                return RES_ETOOBIG;
            if (got == 0)
                return RES_OK;
            continue;
        }

        {
            mqtt_packet_t p;

            r = mqtt_decode(c->rx, need, &p);
            if (r != RES_OK)
                return r;

            r = handle_packet(c, &p, now_ms);
            if (r != RES_OK)
                return r;
        }

        c->rx_len -= need;
        if (c->rx_len > 0)
            memmove(c->rx, c->rx + need, c->rx_len);
    }
}

/* ------------------------------------------------------------------- tick */

static result_t start_connect(mqtt_client_t *c, int64_t now_ms)
{
    mqtt_connect_t o;
    size_t         len = 0;
    result_t       r;

    memset(&o, 0, sizeof(o));
    o.client_id     = c->opt.client_id;
    o.username      = c->opt.username;
    o.password      = c->opt.password;
    o.keepalive_s   = c->opt.keepalive_s;
    o.clean_session = true;
    o.will_topic    = c->opt.will_topic;
    o.will_payload  = c->opt.will_payload;
    o.will_len      = c->opt.will_len;
    o.will_retain   = c->opt.will_retain;

    (void)tx_room(c);
    r = mqtt_encode_connect(c->tx + c->tx_len, sizeof(c->tx) - c->tx_len,
                            &len, &o);
    if (r != RES_OK)
        return r;

    c->tx_len     += len;
    c->state       = MQTTC_AWAIT_CONNACK;
    c->deadline_ms = now_ms + MQTTC_ACK_TIMEOUT_MS;
    c->last_tx_ms  = now_ms;

    return RES_OK;
}

static result_t keepalive(mqtt_client_t *c, int64_t now_ms)
{
    int64_t half = (int64_t)c->opt.keepalive_s * 500;   /* half, in ms */
    size_t  len = 0;

    /* A ping that was never answered means the connection is gone even though
     * the socket still looks open, which is the failure keepalive exists to
     * find. */
    if (c->ping_sent_ms != 0) {
        if (now_ms - c->ping_sent_ms >= (int64_t)c->opt.keepalive_s * 1000)
            return RES_ETIMEOUT;
        return RES_OK;
    }

    if (now_ms - c->last_tx_ms < half)
        return RES_OK;

    if (tx_room(c) < 2u)
        return RES_OK;                  /* the buffer is busy; try next tick */

    if (mqtt_encode_pingreq(c->tx + c->tx_len, sizeof(c->tx) - c->tx_len,
                            &len) != RES_OK)
        return RES_OK;

    c->tx_len      += len;
    c->ping_sent_ms = now_ms;
    c->last_tx_ms   = now_ms;

    return RES_OK;
}

result_t mqttc_tick(mqtt_client_t *c, socket_t *sock, int64_t now_ms)
{
    result_t r;

    if (!c || !sock)
        return RES_EINVAL;
    if (!c->opt.host || c->opt.host[0] == '\0' || !c->opt.client_id)
        return RES_EINVAL;

    if (c->state == MQTTC_DOWN) {
        if (now_ms < c->next_attempt_ms)
            return RES_OK;

        c->state       = MQTTC_CONNECTING;
        c->deadline_ms = now_ms + MQTTC_CONNECT_TIMEOUT_MS;
        c->connect_attempts++;
    }

    if (c->state == MQTTC_CONNECTING) {
        r = sock_connect(sock, c->opt.host, c->opt.port);
        if (r == RES_EAGAIN) {
            if (now_ms >= c->deadline_ms)
                go_down(c, sock, now_ms);
            return RES_OK;
        }
        if (r != RES_OK) {
            go_down(c, sock, now_ms);
            return RES_OK;
        }
        if (start_connect(c, now_ms) != RES_OK) {
            go_down(c, sock, now_ms);
            return RES_OK;
        }
    }

    /* A socket that closed under us is indistinguishable from any other
     * failure from here on. */
    if (!sock_is_connected(sock)) {
        go_down(c, sock, now_ms);
        return RES_OK;
    }

    if (flush_tx(c, sock, now_ms) != RES_OK) {
        go_down(c, sock, now_ms);
        return RES_OK;
    }

    if (pump_rx(c, sock, now_ms) != RES_OK) {
        go_down(c, sock, now_ms);
        return RES_OK;
    }

    if ((c->state == MQTTC_AWAIT_CONNACK || c->state == MQTTC_AWAIT_SUBACK) &&
        now_ms >= c->deadline_ms) {
        go_down(c, sock, now_ms);
        return RES_OK;
    }

    if (c->state == MQTTC_READY) {
        if (keepalive(c, now_ms) != RES_OK) {
            go_down(c, sock, now_ms);
            return RES_OK;
        }
    }

    /* Anything queued while handling this tick's input -- the SUBSCRIBE that
     * follows a CONNACK, a PINGREQ -- goes out now. Leaving it for the next
     * tick would put a whole polling interval between a decision and the
     * packet that carries it. */
    if (flush_tx(c, sock, now_ms) != RES_OK)
        go_down(c, sock, now_ms);

    return RES_OK;
}

result_t mqttc_publish(mqtt_client_t *c, const char *topic,
                       const void *payload, size_t len, bool retain)
{
    size_t   n = 0;
    result_t r;

    if (!c || !topic)
        return RES_EINVAL;
    if (c->state != MQTTC_READY)
        return RES_EIO;

    /* One packet in flight at a time. Growing a queue here would hide a
     * broker that has stopped reading, until memory ran out. */
    if (tx_room(c) < MQTT_PACKET_MAX)
        return RES_EBUSY;

    r = mqtt_encode_publish(c->tx + c->tx_len, sizeof(c->tx) - c->tx_len, &n,
                            topic, payload, len, retain);
    if (r != RES_OK)
        return r;

    c->tx_len += n;

    return RES_OK;
}
