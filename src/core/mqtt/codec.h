/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * MQTT 3.1.1 packet encoding and decoding.
 *
 * Written here rather than linked in: an embedded target may have no package
 * manager worth the name, a Windows build would carry a DLL, and the subset
 * actually needed is small. See docs/adr/0006.
 *
 * Pure functions over caller-supplied buffers. The socket lives behind a port,
 * which is what lets the fiddly part -- lengths, flags, string framing -- be
 * tested without a broker, and that is where the risk in a hand-written
 * protocol implementation actually is.
 *
 * SCOPE: QoS 0 and a clean session, deliberately. Nothing in the payload
 * contract needs delivery guarantees that a reporting interval does not
 * already provide -- a lost report is superseded within one interval, and a
 * control command is acknowledged on its own topic. QoS 1 would buy
 * retransmission state that has to survive a device losing power.
 */
#ifndef TSB_CORE_MQTT_CODEC_H
#define TSB_CORE_MQTT_CODEC_H

#include "core/mqtt/topic.h"
#include "core/util/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A capability list for a fully populated appliance is the largest thing this
 * program ever publishes, and it sets the ceiling. */
#define MQTT_PAYLOAD_MAX   8192u
#define MQTT_PACKET_MAX    (MQTT_PAYLOAD_MAX + TOPIC_MAX + 16u)

/* Protocol level 4 is MQTT 3.1.1. */
#define MQTT_PROTOCOL_LEVEL 4u

typedef enum {
    MQTT_CONNECT     = 1,
    MQTT_CONNACK     = 2,
    MQTT_PUBLISH     = 3,
    MQTT_SUBSCRIBE   = 8,
    MQTT_SUBACK      = 9,
    MQTT_PINGREQ     = 12,
    MQTT_PINGRESP    = 13,
    MQTT_DISCONNECT  = 14
} mqtt_type_t;

/* CONNACK return codes, as the broker sends them. */
#define MQTT_CONNACK_ACCEPTED            0u
#define MQTT_CONNACK_BAD_PROTOCOL        1u
#define MQTT_CONNACK_ID_REJECTED         2u
#define MQTT_CONNACK_UNAVAILABLE         3u
#define MQTT_CONNACK_BAD_CREDENTIALS     4u
#define MQTT_CONNACK_NOT_AUTHORIZED      5u

#define MQTT_SUBACK_FAILURE           0x80u

typedef struct {
    const char *client_id;
    const char *username;      /* NULL for none */
    const char *password;      /* NULL for none */
    uint16_t    keepalive_s;
    bool        clean_session;

    /* The last will. NULL topic means none. */
    const char *will_topic;
    const char *will_payload;
    size_t      will_len;
    bool        will_retain;
} mqtt_connect_t;

typedef struct {
    mqtt_type_t    type;
    uint8_t        flags;        /* the low nibble of byte 0        */
    size_t         total_len;    /* the whole packet, header first  */
    const uint8_t *body;         /* variable header plus payload    */
    size_t         body_len;
} mqtt_packet_t;

/* ------------------------------------------------------------------ encode */

result_t mqtt_encode_connect(uint8_t *out, size_t cap, size_t *len,
                             const mqtt_connect_t *opt);

result_t mqtt_encode_publish(uint8_t *out, size_t cap, size_t *len,
                             const char *topic, const void *payload,
                             size_t payload_len, bool retain);

result_t mqtt_encode_subscribe(uint8_t *out, size_t cap, size_t *len,
                               uint16_t packet_id, const char *topic_filter);

result_t mqtt_encode_pingreq(uint8_t *out, size_t cap, size_t *len);
result_t mqtt_encode_disconnect(uint8_t *out, size_t cap, size_t *len);

/* ------------------------------------------------------------------ decode */

/**
 * How many bytes the packet beginning at `buf` occupies.
 *
 * RES_EAGAIN while the remaining-length field is still incomplete, which is
 * the normal answer on a stream. The same rule as the appliance link: read the
 * header, learn the exact size, then read precisely that much. Guessing leaves
 * the tail of one packet in the buffer, and every later packet is then read at
 * the wrong offset with nothing to detect it.
 */
result_t mqtt_packet_len(const uint8_t *buf, size_t have, size_t *need);

/** Decode a complete packet. `out->body` aliases `buf`. */
result_t mqtt_decode(const uint8_t *buf, size_t len, mqtt_packet_t *out);

result_t mqtt_connack(const mqtt_packet_t *p, bool *session_present,
                      uint8_t *code);

result_t mqtt_publish_parts(const mqtt_packet_t *p,
                            const char **topic, size_t *topic_len,
                            const uint8_t **payload, size_t *payload_len);

result_t mqtt_suback(const mqtt_packet_t *p, uint16_t *packet_id,
                     uint8_t *code);

const char *mqtt_connack_str(uint8_t code);

#endif /* TSB_CORE_MQTT_CODEC_H */
