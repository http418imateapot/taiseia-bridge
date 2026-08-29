/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The MQTT client: connect, subscribe, keep alive, reconnect.
 *
 * Everything here is logic over the socket port, so the whole state machine is
 * exercised in tests by a socket that never touches a network. That is where
 * the interesting failures live -- a broker that accepts the connection and
 * then refuses the credentials, a peer that vanishes mid-packet, a send that
 * only takes half of what it was given.
 *
 * TIME IS A PARAMETER, as everywhere else in the core, so keepalive intervals
 * and reconnect backoff are tested by passing numbers rather than by waiting.
 *
 * NOTHING BLOCKS. Connecting is a loop of RES_EAGAIN rather than a call that
 * stalls the program for seconds against an unreachable broker.
 */
#ifndef TSB_CORE_MQTT_CLIENT_H
#define TSB_CORE_MQTT_CLIENT_H

#include "core/mqtt/codec.h"
#include "core/util/result.h"
#include "ports/socket.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MQTTC_RX_CAP           (MQTT_PACKET_MAX + 64u)
#define MQTTC_TX_CAP           (MQTT_PACKET_MAX + 64u)

/* How long a connect attempt, a CONNACK or a SUBACK may take before the
 * attempt is abandoned and the backoff applies. */
#define MQTTC_CONNECT_TIMEOUT_MS  10000
#define MQTTC_ACK_TIMEOUT_MS      10000

typedef enum {
    MQTTC_DOWN = 0,
    MQTTC_CONNECTING,      /* the socket is still being established     */
    MQTTC_AWAIT_CONNACK,   /* CONNECT is out                            */
    MQTTC_AWAIT_SUBACK,    /* SUBSCRIBE is out                          */
    MQTTC_READY
} mqttc_state_t;

/**
 * Called for each inbound message.
 *
 * The pointers reference the client's receive buffer and are valid only for
 * the duration of the call. A handler that wants to keep the bytes copies
 * them.
 */
typedef void (*mqttc_on_message_fn)(void *ctx,
                                    const char *topic, size_t topic_len,
                                    const uint8_t *payload, size_t len);

/*
 * Strings are borrowed, not copied: they must outlive the client. In practice
 * they point into the loaded configuration, which does.
 */
typedef struct {
    const char *host;
    uint16_t    port;

    const char *client_id;
    const char *username;        /* NULL for none */
    const char *password;        /* NULL for none */
    uint16_t    keepalive_s;

    const char *will_topic;      /* NULL for none */
    const char *will_payload;
    size_t      will_len;
    bool        will_retain;

    const char *subscribe_topic; /* NULL to subscribe to nothing */

    int         reconnect_min_ms;
    int         reconnect_max_ms;

    mqttc_on_message_fn on_message;
    void               *ctx;
} mqttc_options_t;

typedef struct {
    mqttc_options_t opt;
    mqttc_state_t   state;

    uint8_t  rx[MQTTC_RX_CAP];
    size_t   rx_len;

    uint8_t  tx[MQTTC_TX_CAP];
    size_t   tx_len;
    size_t   tx_sent;

    int64_t  next_attempt_ms;
    int      backoff_ms;
    int64_t  deadline_ms;      /* connect, CONNACK or SUBACK              */
    int64_t  last_tx_ms;
    int64_t  ping_sent_ms;     /* 0 when no PINGREQ is outstanding        */

    uint16_t sub_packet_id;
    uint16_t next_packet_id;
    uint8_t  last_connack;
    unsigned connect_attempts;
} mqtt_client_t;

void mqttc_init(mqtt_client_t *c, const mqttc_options_t *opt);

/**
 * Advance by one step: flush what is queued, read what arrived, act on it.
 * Never blocks. Returns RES_EINVAL only for bad arguments; a broker that
 * refuses us is ordinary operation, reported through the state.
 */
result_t mqttc_tick(mqtt_client_t *c, socket_t *sock, int64_t now_ms);

/**
 * Queue a publish. RES_EIO unless the client is ready, RES_EBUSY when the
 * send buffer still holds something -- the caller retries on a later tick
 * rather than the client growing an unbounded queue behind its back.
 */
result_t mqttc_publish(mqtt_client_t *c, const char *topic,
                       const void *payload, size_t len, bool retain);

bool          mqttc_ready(const mqtt_client_t *c);
mqttc_state_t mqttc_state(const mqtt_client_t *c);
uint8_t       mqttc_last_connack(const mqtt_client_t *c);
const char   *mqttc_state_str(mqttc_state_t s);

#endif /* TSB_CORE_MQTT_CLIENT_H */
