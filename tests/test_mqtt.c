/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The fiddly part of a hand-written protocol is lengths, flags and string
 * framing, so the assertions here are on exact bytes rather than on the
 * decoder agreeing with the encoder.
 */
#include "tests.h"

#include "core/mqtt/codec.h"

static void bytes_eq(const uint8_t *got, size_t got_len,
                     const uint8_t *want, size_t want_len, const char *what)
{
    size_t i;

    CHECK_EQ(got_len, want_len);
    if (got_len != want_len)
        return;

    for (i = 0; i < want_len; i++) {
        g_checks++;
        if (got[i] != want[i]) {
            g_failures++;
            fprintf(stderr, "FAIL %s byte %u: got 0x%02X want 0x%02X\n",
                    what, (unsigned)i, got[i], want[i]);
            return;
        }
    }
}

static void a_minimal_connect_is_exact(void)
{
    uint8_t        out[64];
    size_t         len = 0;
    mqtt_connect_t o;
    static const uint8_t want[] = {
        0x10, 0x0D,                             /* CONNECT, 13 remaining   */
        0x00, 0x04, 'M', 'Q', 'T', 'T',         /* protocol name           */
        0x04,                                   /* level 4 = MQTT 3.1.1    */
        0x02,                                   /* clean session           */
        0x00, 0x3C,                             /* keepalive 60            */
        0x00, 0x01, 'C'                         /* client id               */
    };

    memset(&o, 0, sizeof(o));
    o.client_id     = "C";
    o.keepalive_s   = 60;
    o.clean_session = true;

    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), &len, &o), RES_OK);
    bytes_eq(out, len, want, sizeof(want), "connect");
}

static void a_connect_with_credentials_and_a_will(void)
{
    uint8_t        out[128];
    size_t         len = 0;
    mqtt_connect_t o;
    static const uint8_t want[] = {
        0x10, 0x1D,                             /* CONNECT, 29 remaining   */
        0x00, 0x04, 'M', 'Q', 'T', 'T',
        0x04,
        0xE6,                    /* username|password|will retain|will|clean */
        0x00, 0x0A,                             /* keepalive 10            */
        0x00, 0x02, 'i', 'd',                   /* client id               */
        0x00, 0x03, 'w', '/', 't',              /* will topic              */
        0x00, 0x02, 'b', 'y',                   /* will payload            */
        0x00, 0x01, 'u',                        /* username                */
        0x00, 0x01, 'p'                         /* password                */
    };

    memset(&o, 0, sizeof(o));
    o.client_id     = "id";
    o.username      = "u";
    o.password      = "p";
    o.keepalive_s   = 10;
    o.clean_session = true;
    o.will_topic    = "w/t";
    o.will_payload  = "by";
    o.will_len      = 2;
    o.will_retain   = true;

    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), &len, &o), RES_OK);
    bytes_eq(out, len, want, sizeof(want), "connect+will");
}

static void connect_refuses_what_the_protocol_forbids(void)
{
    uint8_t        out[64];
    size_t         len = 0;
    mqtt_connect_t o;

    memset(&o, 0, sizeof(o));
    o.client_id = "C";

    CHECK_EQ(mqtt_encode_connect(NULL, sizeof(out), &len, &o), RES_EINVAL);
    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), NULL, &o), RES_EINVAL);
    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), &len, NULL), RES_EINVAL);

    /* A password without a username is not allowed in 3.1.1. */
    o.password = "p";
    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), &len, &o), RES_EINVAL);
    o.password = NULL;

    o.client_id = "";
    CHECK_EQ(mqtt_encode_connect(out, sizeof(out), &len, &o), RES_EINVAL);
    o.client_id = "C";

    CHECK_EQ(mqtt_encode_connect(out, 4, &len, &o), RES_ETOOBIG);
}

static void publish_and_subscribe_are_exact(void)
{
    uint8_t out[64];
    size_t  len = 0;
    static const uint8_t want_pub[] = {
        0x30, 0x07,                             /* PUBLISH, 7 remaining    */
        0x00, 0x03, 'a', '/', 'b',
        'h', 'i'
    };
    static const uint8_t want_ret[] = {
        0x31, 0x05,                             /* retain bit set          */
        0x00, 0x03, 'a', '/', 'b'
    };
    static const uint8_t want_sub[] = {
        0x82, 0x08,                             /* SUBSCRIBE, reserved 2   */
        0x00, 0x01,                             /* packet id               */
        0x00, 0x03, 'a', '/', 'b',
        0x00                                    /* requested QoS 0         */
    };
    static const uint8_t want_ping[] = { 0xC0, 0x00 };
    static const uint8_t want_disc[] = { 0xE0, 0x00 };

    CHECK_EQ(mqtt_encode_publish(out, sizeof(out), &len, "a/b", "hi", 2,
                                 false), RES_OK);
    bytes_eq(out, len, want_pub, sizeof(want_pub), "publish");

    CHECK_EQ(mqtt_encode_publish(out, sizeof(out), &len, "a/b", NULL, 0,
                                 true), RES_OK);
    bytes_eq(out, len, want_ret, sizeof(want_ret), "publish retained");

    CHECK_EQ(mqtt_encode_subscribe(out, sizeof(out), &len, 1, "a/b"), RES_OK);
    bytes_eq(out, len, want_sub, sizeof(want_sub), "subscribe");

    CHECK_EQ(mqtt_encode_pingreq(out, sizeof(out), &len), RES_OK);
    bytes_eq(out, len, want_ping, sizeof(want_ping), "pingreq");

    CHECK_EQ(mqtt_encode_disconnect(out, sizeof(out), &len), RES_OK);
    bytes_eq(out, len, want_disc, sizeof(want_disc), "disconnect");

    CHECK_EQ(mqtt_encode_publish(out, sizeof(out), &len, "", "x", 1, false),
             RES_EINVAL);
    CHECK_EQ(mqtt_encode_publish(out, sizeof(out), &len, "t", NULL, 1, false),
             RES_EINVAL);
    /* A subscribe with no packet identifier could not be matched to its
     * acknowledgement. */
    CHECK_EQ(mqtt_encode_subscribe(out, sizeof(out), &len, 0, "a"),
             RES_EINVAL);
    CHECK_EQ(mqtt_encode_pingreq(out, 1, &len), RES_ETOOBIG);
}

/*
 * Read the header, learn the exact size, read precisely that much. The same
 * rule as the appliance link, for the same reason: guessing leaves the tail of
 * one packet in the buffer and every later packet is read at the wrong offset.
 */
static void a_length_is_learned_before_the_body_is_read(void)
{
    uint8_t big[512];
    uint8_t payload[200];
    size_t  len = 0;
    size_t  need = 0;
    size_t  i;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;

    /* 203 bytes remaining needs two length bytes: 0xCB 0x01. */
    CHECK_EQ(mqtt_encode_publish(big, sizeof(big), &len, "t", payload,
                                 sizeof(payload), false), RES_OK);
    CHECK_EQ(len, 1 + 2 + 203);
    CHECK_EQ(big[1], 0xCB);
    CHECK_EQ(big[2], 0x01);

    /* One byte is not enough to know the size; nor are the first two, since
     * the length field continues. */
    CHECK_EQ(mqtt_packet_len(big, 0, &need), RES_EAGAIN);
    CHECK_EQ(mqtt_packet_len(big, 1, &need), RES_EAGAIN);
    CHECK_EQ(mqtt_packet_len(big, 2, &need), RES_EAGAIN);
    CHECK_EQ(mqtt_packet_len(big, 3, &need), RES_OK);
    CHECK_EQ(need, len);

    /* Single-byte case. */
    CHECK_EQ(mqtt_encode_pingreq(big, sizeof(big), &len), RES_OK);
    CHECK_EQ(mqtt_packet_len(big, 2, &need), RES_OK);
    CHECK_EQ(need, 2);

    /* A continuation bit on the fourth length byte is malformed. */
    {
        static const uint8_t bad[] = { 0x30, 0xFF, 0xFF, 0xFF, 0xFF };

        CHECK_EQ(mqtt_packet_len(bad, sizeof(bad), &need), RES_EFORMAT);
    }

    /* And a length beyond anything we will hold is refused, not attempted. */
    {
        static const uint8_t huge[] = { 0x30, 0xFF, 0xFF, 0xFF, 0x7F };

        CHECK_EQ(mqtt_packet_len(huge, sizeof(huge), &need), RES_ETOOBIG);
    }

    CHECK_EQ(mqtt_packet_len(NULL, 2, &need), RES_EINVAL);
    CHECK_EQ(mqtt_packet_len(big, 2, NULL), RES_EINVAL);
}

static void a_published_message_round_trips(void)
{
    uint8_t       buf[256];
    size_t        len = 0;
    size_t        need = 0;
    mqtt_packet_t p;
    const char   *topic = NULL;
    size_t        topic_len = 0;
    const uint8_t *payload = NULL;
    size_t        payload_len = 0;

    CHECK_EQ(mqtt_encode_publish(buf, sizeof(buf), &len,
                                 "v1/to/device/TS-1/control",
                                 "{\"tag\":1}", 9, false), RES_OK);
    CHECK_EQ(mqtt_packet_len(buf, len, &need), RES_OK);
    CHECK_EQ(need, len);

    CHECK_EQ(mqtt_decode(buf, len, &p), RES_OK);
    CHECK_EQ(p.type, MQTT_PUBLISH);
    CHECK_EQ(p.total_len, len);

    CHECK_EQ(mqtt_publish_parts(&p, &topic, &topic_len, &payload,
                                &payload_len), RES_OK);
    CHECK_EQ(topic_len, 25);
    CHECK(memcmp(topic, "v1/to/device/TS-1/control", 25) == 0);
    CHECK_EQ(payload_len, 9);
    CHECK(memcmp(payload, "{\"tag\":1}", 9) == 0);
}

static void acknowledgements_decode(void)
{
    static const uint8_t connack[]  = { 0x20, 0x02, 0x01, 0x00 };
    static const uint8_t refused[]  = { 0x20, 0x02, 0x00, 0x05 };
    static const uint8_t suback[]   = { 0x90, 0x03, 0x00, 0x07, 0x00 };
    static const uint8_t subfail[]  = { 0x90, 0x03, 0x00, 0x07, 0x80 };
    static const uint8_t pingresp[] = { 0xD0, 0x00 };
    mqtt_packet_t p;
    bool          present = false;
    uint8_t       code = 0xFF;
    uint16_t      id = 0;

    CHECK_EQ(mqtt_decode(connack, sizeof(connack), &p), RES_OK);
    CHECK_EQ(mqtt_connack(&p, &present, &code), RES_OK);
    CHECK_EQ(present, 1);
    CHECK_EQ(code, MQTT_CONNACK_ACCEPTED);

    CHECK_EQ(mqtt_decode(refused, sizeof(refused), &p), RES_OK);
    CHECK_EQ(mqtt_connack(&p, &present, &code), RES_OK);
    CHECK_EQ(present, 0);
    CHECK_EQ(code, MQTT_CONNACK_NOT_AUTHORIZED);
    CHECK_STREQ(mqtt_connack_str(code), "not authorised");
    CHECK_STREQ(mqtt_connack_str(99), "unknown");

    CHECK_EQ(mqtt_decode(suback, sizeof(suback), &p), RES_OK);
    CHECK_EQ(mqtt_suback(&p, &id, &code), RES_OK);
    CHECK_EQ(id, 7);
    CHECK_EQ(code, 0);

    CHECK_EQ(mqtt_decode(subfail, sizeof(subfail), &p), RES_OK);
    CHECK_EQ(mqtt_suback(&p, &id, &code), RES_OK);
    CHECK_EQ(code, MQTT_SUBACK_FAILURE);

    CHECK_EQ(mqtt_decode(pingresp, sizeof(pingresp), &p), RES_OK);
    CHECK_EQ(p.type, MQTT_PINGRESP);
    CHECK_EQ(p.body_len, 0);

    /* Asking the wrong accessor for a packet is a format error, not a
     * misreading of whatever bytes happen to be there. */
    CHECK_EQ(mqtt_connack(&p, &present, &code), RES_EFORMAT);
    CHECK_EQ(mqtt_suback(&p, &id, &code), RES_EFORMAT);
}

static void malformed_packets_are_refused(void)
{
    static const uint8_t short_connack[] = { 0x20, 0x01, 0x00 };
    static const uint8_t zero_type[]     = { 0x00, 0x00 };
    /* PUBLISH with QoS 1: the variable header would carry a packet id we do
     * not handle, so reading the topic our way would be wrong. */
    static const uint8_t qos1[] = {
        0x32, 0x07, 0x00, 0x01, 't', 0x00, 0x01, 'h', 'i'
    };
    /* A topic length longer than the body it sits in. */
    static const uint8_t bad_topic[] = { 0x30, 0x04, 0x00, 0x40, 'a', 'b' };
    mqtt_packet_t p;
    bool          present = false;
    uint8_t       code = 0;
    const char   *topic = NULL;
    size_t        tl = 0, pl = 0;
    const uint8_t *pay = NULL;

    CHECK_EQ(mqtt_decode(short_connack, sizeof(short_connack), &p), RES_OK);
    CHECK_EQ(mqtt_connack(&p, &present, &code), RES_EFORMAT);

    CHECK_EQ(mqtt_decode(zero_type, sizeof(zero_type), &p), RES_EFORMAT);

    CHECK_EQ(mqtt_decode(qos1, sizeof(qos1), &p), RES_OK);
    CHECK_EQ(mqtt_publish_parts(&p, &topic, &tl, &pay, &pl),
             RES_EUNSUPPORTED);

    CHECK_EQ(mqtt_decode(bad_topic, sizeof(bad_topic), &p), RES_OK);
    CHECK_EQ(mqtt_publish_parts(&p, &topic, &tl, &pay, &pl), RES_EFORMAT);

    /* A declared length that disagrees with the bytes held. */
    {
        static const uint8_t pkt[] = { 0x30, 0x07, 0x00, 0x03, 'a', '/', 'b' };

        CHECK_EQ(mqtt_decode(pkt, sizeof(pkt), &p), RES_EFORMAT);
    }

    CHECK_EQ(mqtt_decode(NULL, 2, &p), RES_EINVAL);
    CHECK_EQ(mqtt_decode(zero_type, 2, NULL), RES_EINVAL);
    CHECK_EQ(mqtt_connack(NULL, &present, &code), RES_EINVAL);
    CHECK_EQ(mqtt_publish_parts(NULL, &topic, &tl, &pay, &pl), RES_EINVAL);
    CHECK_EQ(mqtt_suback(NULL, NULL, NULL), RES_EINVAL);
}

void test_mqtt(void)
{
    a_minimal_connect_is_exact();
    a_connect_with_credentials_and_a_will();
    connect_refuses_what_the_protocol_forbids();
    publish_and_subscribe_are_exact();
    a_length_is_learned_before_the_body_is_read();
    a_published_message_round_trips();
    acknowledgements_decode();
    malformed_packets_are_refused();
}
