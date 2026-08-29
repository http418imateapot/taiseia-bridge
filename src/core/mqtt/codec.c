/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/mqtt/codec.h"

#include <string.h>

/* A remaining-length field is at most four bytes and encodes at most 256 MB;
 * we cap far below that, since nothing here is remotely that large. */
#define REM_LEN_MAX_BYTES 4u

/* ------------------------------------------------------------------ writer */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
} wbuf_t;

static void w_bytes(wbuf_t *w, const void *p, size_t n)
{
    if (w->overflow)
        return;
    if (n > w->cap - w->len) {
        w->overflow = true;
        return;
    }
    if (n > 0)
        memcpy(w->buf + w->len, p, n);
    w->len += n;
}

static void w_u8(wbuf_t *w, uint8_t v)
{
    w_bytes(w, &v, 1);
}

static void w_u16(wbuf_t *w, uint16_t v)
{
    uint8_t b[2];

    b[0] = (uint8_t)((v >> 8) & 0xFFu);
    b[1] = (uint8_t)(v & 0xFFu);
    w_bytes(w, b, 2);
}

/* An MQTT string: a two-byte length, then the bytes. */
static void w_str(wbuf_t *w, const char *s)
{
    size_t n;

    if (!s) {
        w->overflow = true;
        return;
    }
    n = strlen(s);
    if (n > 0xFFFFu) {
        w->overflow = true;
        return;
    }
    w_u16(w, (uint16_t)n);
    w_bytes(w, s, n);
}

static size_t str_field_len(const char *s)
{
    return 2u + (s ? strlen(s) : 0u);
}

static void w_rem_len(wbuf_t *w, size_t rem)
{
    unsigned n = 0;

    do {
        uint8_t b = (uint8_t)(rem % 128u);

        rem /= 128u;
        if (rem > 0)
            b = (uint8_t)(b | 0x80u);
        w_u8(w, b);
        n++;
    } while (rem > 0 && n < REM_LEN_MAX_BYTES);

    if (rem > 0)
        w->overflow = true;
}

static result_t w_finish(wbuf_t *w, size_t *len)
{
    if (w->overflow)
        return RES_ETOOBIG;

    *len = w->len;
    return RES_OK;
}

static void w_init(wbuf_t *w, uint8_t *buf, size_t cap)
{
    w->buf      = buf;
    w->cap      = cap;
    w->len      = 0;
    w->overflow = false;
}

/* ------------------------------------------------------------------ encode */

result_t mqtt_encode_connect(uint8_t *out, size_t cap, size_t *len,
                             const mqtt_connect_t *o)
{
    wbuf_t  w;
    size_t  rem;
    uint8_t flags = 0;

    if (!out || !len || !o || !o->client_id)
        return RES_EINVAL;
    /* A zero-length client id is only legal with a clean session, and this
     * program has no use for a broker-assigned one. */
    if (o->client_id[0] == '\0')
        return RES_EINVAL;
    if (o->will_topic && (!o->will_payload && o->will_len > 0))
        return RES_EINVAL;
    if (o->password && !o->username)
        return RES_EINVAL;   /* 3.1.1 forbids a password without a username */

    /* protocol name + level + flags + keepalive */
    rem = str_field_len("MQTT") + 1u + 1u + 2u;
    rem += str_field_len(o->client_id);
    if (o->will_topic)
        rem += str_field_len(o->will_topic) + 2u + o->will_len;
    if (o->username)
        rem += str_field_len(o->username);
    if (o->password)
        rem += str_field_len(o->password);

    if (o->clean_session) flags = (uint8_t)(flags | 0x02u);
    if (o->will_topic) {
        flags = (uint8_t)(flags | 0x04u);          /* will flag, QoS 0 */
        if (o->will_retain)
            flags = (uint8_t)(flags | 0x20u);
    }
    if (o->password) flags = (uint8_t)(flags | 0x40u);
    if (o->username) flags = (uint8_t)(flags | 0x80u);

    w_init(&w, out, cap);
    w_u8(&w, (uint8_t)(MQTT_CONNECT << 4));
    w_rem_len(&w, rem);
    w_str(&w, "MQTT");
    w_u8(&w, MQTT_PROTOCOL_LEVEL);
    w_u8(&w, flags);
    w_u16(&w, o->keepalive_s);

    w_str(&w, o->client_id);
    if (o->will_topic) {
        w_str(&w, o->will_topic);
        if (o->will_len > 0xFFFFu) {
            w.overflow = true;
        } else {
            w_u16(&w, (uint16_t)o->will_len);
            w_bytes(&w, o->will_payload, o->will_len);
        }
    }
    if (o->username)
        w_str(&w, o->username);
    if (o->password)
        w_str(&w, o->password);

    return w_finish(&w, len);
}

result_t mqtt_encode_publish(uint8_t *out, size_t cap, size_t *len,
                             const char *topic, const void *payload,
                             size_t payload_len, bool retain)
{
    wbuf_t w;
    size_t rem;

    if (!out || !len || !topic || topic[0] == '\0')
        return RES_EINVAL;
    if (payload_len > 0 && !payload)
        return RES_EINVAL;
    if (payload_len > MQTT_PAYLOAD_MAX)
        return RES_ETOOBIG;

    /* QoS 0 carries no packet identifier, so the variable header is just the
     * topic. */
    rem = str_field_len(topic) + payload_len;

    w_init(&w, out, cap);
    w_u8(&w, (uint8_t)((MQTT_PUBLISH << 4) | (retain ? 0x01u : 0x00u)));
    w_rem_len(&w, rem);
    w_str(&w, topic);
    w_bytes(&w, payload, payload_len);

    return w_finish(&w, len);
}

result_t mqtt_encode_subscribe(uint8_t *out, size_t cap, size_t *len,
                               uint16_t packet_id, const char *topic_filter)
{
    wbuf_t w;
    size_t rem;

    if (!out || !len || !topic_filter || topic_filter[0] == '\0')
        return RES_EINVAL;
    /* A packet identifier of zero is not allowed for a packet that expects an
     * acknowledgement -- the reply could not be matched to anything. */
    if (packet_id == 0)
        return RES_EINVAL;

    rem = 2u + str_field_len(topic_filter) + 1u;

    w_init(&w, out, cap);
    w_u8(&w, (uint8_t)((MQTT_SUBSCRIBE << 4) | 0x02u));   /* reserved bits */
    w_rem_len(&w, rem);
    w_u16(&w, packet_id);
    w_str(&w, topic_filter);
    w_u8(&w, 0);                                          /* requested QoS 0 */

    return w_finish(&w, len);
}

static result_t encode_bare(uint8_t *out, size_t cap, size_t *len,
                            mqtt_type_t type)
{
    if (!out || !len)
        return RES_EINVAL;
    if (cap < 2u)
        return RES_ETOOBIG;

    out[0] = (uint8_t)(type << 4);
    out[1] = 0;
    *len   = 2u;

    return RES_OK;
}

result_t mqtt_encode_pingreq(uint8_t *out, size_t cap, size_t *len)
{
    return encode_bare(out, cap, len, MQTT_PINGREQ);
}

result_t mqtt_encode_disconnect(uint8_t *out, size_t cap, size_t *len)
{
    return encode_bare(out, cap, len, MQTT_DISCONNECT);
}

/* ------------------------------------------------------------------ decode */

static result_t read_rem_len(const uint8_t *buf, size_t have, size_t *value,
                             size_t *nbytes)
{
    size_t   mult = 1;
    size_t   val = 0;
    unsigned i = 0;

    for (;;) {
        uint8_t b;

        if (i >= REM_LEN_MAX_BYTES)
            return RES_EFORMAT;         /* more than four bytes is malformed */
        if (1u + i >= have)
            return RES_EAGAIN;

        b = buf[1u + i];
        val += (size_t)(b & 0x7Fu) * mult;
        mult *= 128u;
        i++;

        if ((b & 0x80u) == 0)
            break;
    }

    *value  = val;
    *nbytes = i;

    return RES_OK;
}

result_t mqtt_packet_len(const uint8_t *buf, size_t have, size_t *need)
{
    size_t   rem = 0;
    size_t   n = 0;
    result_t r;

    if (!buf || !need)
        return RES_EINVAL;
    if (have < 1u)
        return RES_EAGAIN;

    r = read_rem_len(buf, have, &rem, &n);
    if (r != RES_OK)
        return r;

    if (rem > MQTT_PACKET_MAX)
        return RES_ETOOBIG;

    *need = 1u + n + rem;

    return RES_OK;
}

result_t mqtt_decode(const uint8_t *buf, size_t len, mqtt_packet_t *out)
{
    size_t   rem = 0;
    size_t   n = 0;
    size_t   total;
    result_t r;
    uint8_t  type;

    if (!buf || !out)
        return RES_EINVAL;

    memset(out, 0, sizeof(*out));

    r = read_rem_len(buf, len, &rem, &n);
    if (r != RES_OK)
        return r;

    total = 1u + n + rem;
    if (total != len)
        return RES_EFORMAT;

    type = (uint8_t)((buf[0] >> 4) & 0x0Fu);
    if (type == 0 || type > 14u)
        return RES_EFORMAT;

    out->type      = (mqtt_type_t)type;
    out->flags     = (uint8_t)(buf[0] & 0x0Fu);
    out->total_len = total;
    out->body      = buf + 1u + n;
    out->body_len  = rem;

    return RES_OK;
}

result_t mqtt_connack(const mqtt_packet_t *p, bool *session_present,
                      uint8_t *code)
{
    if (!p || !session_present || !code)
        return RES_EINVAL;
    if (p->type != MQTT_CONNACK || p->body_len != 2u)
        return RES_EFORMAT;

    *session_present = (p->body[0] & 0x01u) != 0;
    *code            = p->body[1];

    return RES_OK;
}

result_t mqtt_publish_parts(const mqtt_packet_t *p,
                            const char **topic, size_t *topic_len,
                            const uint8_t **payload, size_t *payload_len)
{
    size_t tlen;

    if (!p || !topic || !topic_len || !payload || !payload_len)
        return RES_EINVAL;
    if (p->type != MQTT_PUBLISH)
        return RES_EFORMAT;

    /* QoS above 0 would carry a packet identifier here, and we neither ask
     * for nor handle one. Refuse rather than misread the topic. */
    if (((p->flags >> 1) & 0x03u) != 0)
        return RES_EUNSUPPORTED;

    if (p->body_len < 2u)
        return RES_EFORMAT;

    tlen = ((size_t)p->body[0] << 8) | p->body[1];
    if (2u + tlen > p->body_len)
        return RES_EFORMAT;

    *topic       = (const char *)(p->body + 2);
    *topic_len   = tlen;
    *payload     = p->body + 2u + tlen;
    *payload_len = p->body_len - 2u - tlen;

    return RES_OK;
}

result_t mqtt_suback(const mqtt_packet_t *p, uint16_t *packet_id,
                     uint8_t *code)
{
    if (!p || !packet_id || !code)
        return RES_EINVAL;
    if (p->type != MQTT_SUBACK || p->body_len != 3u)
        return RES_EFORMAT;

    *packet_id = (uint16_t)(((uint16_t)p->body[0] << 8) | p->body[1]);
    *code      = p->body[2];

    return RES_OK;
}

const char *mqtt_connack_str(uint8_t code)
{
    switch (code) {
    case MQTT_CONNACK_ACCEPTED:        return "accepted";
    case MQTT_CONNACK_BAD_PROTOCOL:    return "unacceptable protocol version";
    case MQTT_CONNACK_ID_REJECTED:     return "client id rejected";
    case MQTT_CONNACK_UNAVAILABLE:     return "server unavailable";
    case MQTT_CONNACK_BAD_CREDENTIALS: return "bad username or password";
    case MQTT_CONNACK_NOT_AUTHORIZED:  return "not authorised";
    default:                           return "unknown";
    }
}
