/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * The client is logic over the socket port, so a socket that never touches a
 * network exercises all of it -- including the cases a real broker will not
 * reproduce on demand: a connection accepted and then refused, a peer that
 * vanishes mid-packet, a send that takes only one byte.
 */
#include "tests.h"

#include "core/mqtt/client.h"
#include "ports/socket.h"

/* ------------------------------------------------------------ fake socket */

typedef struct {
    bool     connected;
    int      connect_eagain;   /* ticks of "in progress" before connecting */
    bool     connect_fails;
    bool     peer_gone;

    uint8_t  out[4096];        /* what the client sent us */
    size_t   out_len;
    size_t   send_limit;       /* bytes accepted per call; 0 is unlimited */

    uint8_t  in[4096];         /* what we will feed the client */
    size_t   in_len;
    size_t   in_pos;
    size_t   recv_limit;

    unsigned closes;
} fake_sock_t;

static result_t fs_connect(void *self, const char *host, uint16_t port)
{
    fake_sock_t *f = (fake_sock_t *)self;

    (void)host;
    (void)port;

    if (f->connect_fails)
        return RES_EIO;
    if (f->connect_eagain > 0) {
        f->connect_eagain--;
        return RES_EAGAIN;
    }
    f->connected = true;
    return RES_OK;
}

static void fs_close(void *self)
{
    fake_sock_t *f = (fake_sock_t *)self;

    if (f->connected)
        f->closes++;
    f->connected = false;
}

static result_t fs_send(void *self, const uint8_t *buf, size_t n, size_t *sent)
{
    fake_sock_t *f = (fake_sock_t *)self;
    size_t       take = n;

    if (!f->connected || f->peer_gone)
        return RES_EIO;
    if (f->send_limit > 0 && take > f->send_limit)
        take = f->send_limit;
    if (take > sizeof(f->out) - f->out_len)
        take = sizeof(f->out) - f->out_len;
    if (take == 0)
        return RES_EAGAIN;

    memcpy(f->out + f->out_len, buf, take);
    f->out_len += take;
    *sent = take;

    return RES_OK;
}

static result_t fs_recv(void *self, uint8_t *buf, size_t cap, size_t *got)
{
    fake_sock_t *f = (fake_sock_t *)self;
    size_t       avail;

    if (!f->connected)
        return RES_EIO;
    if (f->peer_gone)
        return RES_EIO;

    avail = f->in_len - f->in_pos;
    if (avail == 0)
        return RES_EAGAIN;
    if (avail > cap)
        avail = cap;
    if (f->recv_limit > 0 && avail > f->recv_limit)
        avail = f->recv_limit;

    memcpy(buf, f->in + f->in_pos, avail);
    f->in_pos += avail;
    *got = avail;

    return RES_OK;
}

static bool fs_connected(void *self)
{
    fake_sock_t *f = (fake_sock_t *)self;

    return f->connected && !f->peer_gone;
}

static const socket_vt_t k_fake_vt = {
    fs_connect, fs_close, fs_send, fs_recv, fs_connected
};

static void fake_bind(fake_sock_t *f, socket_t *s)
{
    memset(f, 0, sizeof(*f));
    s->vt   = &k_fake_vt;
    s->self = f;
}

static void feed(fake_sock_t *f, const uint8_t *p, size_t n)
{
    CHECK(n <= sizeof(f->in) - f->in_len);
    memcpy(f->in + f->in_len, p, n);
    f->in_len += n;
}

/* --------------------------------------------------------------- fixtures */

static const uint8_t k_connack_ok[]  = { 0x20, 0x02, 0x00, 0x00 };
static const uint8_t k_connack_bad[] = { 0x20, 0x02, 0x00, 0x05 };
static const uint8_t k_suback_ok[]   = { 0x90, 0x03, 0x00, 0x01, 0x00 };
static const uint8_t k_suback_bad[]  = { 0x90, 0x03, 0x00, 0x01, 0x80 };
static const uint8_t k_pingresp[]    = { 0xD0, 0x00 };

static int   g_msgs;
static char  g_topic[128];
static char  g_payload[128];

static void on_message(void *ctx, const char *topic, size_t topic_len,
                       const uint8_t *payload, size_t len)
{
    (void)ctx;

    g_msgs++;
    if (topic_len < sizeof(g_topic)) {
        memcpy(g_topic, topic, topic_len);
        g_topic[topic_len] = '\0';
    }
    if (len < sizeof(g_payload)) {
        memcpy(g_payload, payload, len);
        g_payload[len] = '\0';
    }
}

static void options(mqttc_options_t *o)
{
    memset(o, 0, sizeof(*o));
    o->host             = "broker";
    o->port             = 1883;
    o->client_id        = "C";
    o->keepalive_s      = 10;
    o->subscribe_topic  = "taiseia101/v1/to/appliance/C/command";
    o->reconnect_min_ms = 100;
    o->reconnect_max_ms = 800;
    o->on_message       = on_message;
}

/* Drive to READY, and return the tick count consumed. */
static void bring_up(mqtt_client_t *c, fake_sock_t *f, socket_t *s)
{
    mqttc_options_t o;

    options(&o);
    mqttc_init(c, &o);
    fake_bind(f, s);

    CHECK_EQ(mqttc_tick(c, s, 0), RES_OK);          /* connect, send CONNECT */
    CHECK_EQ(mqttc_state(c), MQTTC_AWAIT_CONNACK);

    feed(f, k_connack_ok, sizeof(k_connack_ok));
    CHECK_EQ(mqttc_tick(c, s, 0), RES_OK);          /* connack, send SUBSCRIBE */
    CHECK_EQ(mqttc_state(c), MQTTC_AWAIT_SUBACK);

    feed(f, k_suback_ok, sizeof(k_suback_ok));
    CHECK_EQ(mqttc_tick(c, s, 0), RES_OK);
    CHECK_EQ(mqttc_state(c), MQTTC_READY);
    CHECK(mqttc_ready(c));
}

/* ------------------------------------------------------------------ tests */

static void it_connects_subscribes_and_becomes_ready(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;

    bring_up(&c, &f, &s);

    /* CONNECT then SUBSCRIBE, in that order, on the wire. */
    CHECK_EQ(f.out[0], 0x10);
    CHECK(f.out_len > 15);
    {
        size_t i;
        int    saw_subscribe = 0;

        for (i = 0; i + 1 < f.out_len; i++) {
            if (f.out[i] == 0x82) {
                saw_subscribe = 1;
                break;
            }
        }
        CHECK(saw_subscribe);
    }
    CHECK_EQ(mqttc_last_connack(&c), MQTT_CONNACK_ACCEPTED);
    CHECK_STREQ(mqttc_state_str(MQTTC_READY), "ready");
}

/*
 * Bad credentials are not a transport problem, and the connection was accepted
 * before they were refused. Retrying at full speed would just hammer the
 * broker, so the ordinary backoff applies.
 */
static void a_refused_connack_takes_the_client_down(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    feed(&f, k_connack_bad, sizeof(k_connack_bad));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);

    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
    CHECK_EQ(mqttc_last_connack(&c), MQTT_CONNACK_NOT_AUTHORIZED);
    CHECK_EQ(f.closes, 1);

    /* And it waits before trying again. */
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
    CHECK_EQ(mqttc_tick(&c, &s, 100), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_AWAIT_CONNACK);
}

/*
 * Without the control topic the bridge is publish-only, which is not what was
 * asked for. Better to start again than to run degraded and say nothing.
 */
static void a_refused_subscription_takes_the_client_down(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    feed(&f, k_connack_ok, sizeof(k_connack_ok));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    feed(&f, k_suback_bad, sizeof(k_suback_bad));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);

    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void a_client_with_nothing_to_subscribe_to_is_ready_at_connack(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    o.subscribe_topic = NULL;
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    feed(&f, k_connack_ok, sizeof(k_connack_ok));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_READY);
}

static void a_connect_that_never_completes_is_abandoned(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);
    f.connect_eagain = 1000;

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_CONNECTING);

    CHECK_EQ(mqttc_tick(&c, &s, MQTTC_CONNECT_TIMEOUT_MS - 1), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_CONNECTING);

    CHECK_EQ(mqttc_tick(&c, &s, MQTTC_CONNECT_TIMEOUT_MS), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void a_connack_that_never_arrives_is_abandoned(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_AWAIT_CONNACK);

    CHECK_EQ(mqttc_tick(&c, &s, MQTTC_ACK_TIMEOUT_MS - 1), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_AWAIT_CONNACK);

    CHECK_EQ(mqttc_tick(&c, &s, MQTTC_ACK_TIMEOUT_MS), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void a_failing_connect_backs_off_and_caps(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;
    int64_t         t = 0;
    int             i;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);
    f.connect_fails = true;

    for (i = 0; i < 20; i++) {
        CHECK_EQ(mqttc_tick(&c, &s, t), RES_OK);
        t += 1000;
    }
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
    CHECK(c.backoff_ms <= 800);
    CHECK(c.backoff_ms >= 100);
    CHECK(c.connect_attempts > 1);
}

/*
 * A ping that is never answered means the connection is gone even though the
 * socket still looks open. That is the failure keepalive exists to find.
 */
static void keepalive_pings_and_gives_up_when_unanswered(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;
    size_t        before;

    bring_up(&c, &f, &s);
    before = f.out_len;

    /* Nothing to say before half the keepalive has passed. */
    CHECK_EQ(mqttc_tick(&c, &s, 4000), RES_OK);
    CHECK_EQ(f.out_len, before);

    CHECK_EQ(mqttc_tick(&c, &s, 5000), RES_OK);
    CHECK_EQ(f.out_len, before + 2);
    CHECK_EQ(f.out[before], 0xC0);          /* PINGREQ */

    /* Answered: the client settles. */
    feed(&f, k_pingresp, sizeof(k_pingresp));
    CHECK_EQ(mqttc_tick(&c, &s, 5001), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_READY);

    /* Unanswered for a whole keepalive period: the link is judged dead. */
    CHECK_EQ(mqttc_tick(&c, &s, 10001), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_READY);
    CHECK_EQ(mqttc_tick(&c, &s, 20002), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void an_inbound_publish_reaches_the_handler(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;
    static const uint8_t pub[] = {
        0x30, 0x0E,
        0x00, 0x03, 'a', '/', 'b',
        '{', '"', 't', 'a', 'g', '"', ':', '1', '}'
    };

    bring_up(&c, &f, &s);
    g_msgs = 0;

    feed(&f, pub, sizeof(pub));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);

    CHECK_EQ(g_msgs, 1);
    CHECK_STREQ(g_topic, "a/b");
    CHECK_STREQ(g_payload, "{\"tag\":1}");
    CHECK_EQ(mqttc_state(&c), MQTTC_READY);
}

/* Two packets in one read, and one packet across two reads. */
static void packet_boundaries_do_not_follow_read_boundaries(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;
    static const uint8_t two[] = {
        0x30, 0x06, 0x00, 0x03, 'a', '/', 'b', 'x',
        0x30, 0x06, 0x00, 0x03, 'c', '/', 'd', 'y'
    };

    bring_up(&c, &f, &s);
    g_msgs = 0;

    feed(&f, two, sizeof(two));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(g_msgs, 2);
    CHECK_STREQ(g_topic, "c/d");

    /* Now one byte per read, so every packet arrives in pieces. */
    bring_up(&c, &f, &s);
    g_msgs = 0;
    f.recv_limit = 1;
    feed(&f, two, sizeof(two));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(g_msgs, 2);
    CHECK_STREQ(g_payload, "y");
}

/* A send that takes one byte at a time must not corrupt the stream. */
static void a_partial_send_is_resumed(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;
    int             i;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);
    f.send_limit = 1;

    for (i = 0; i < 40; i++)
        CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);

    /* The whole CONNECT eventually went out, byte by byte, intact. */
    CHECK_EQ(f.out[0], 0x10);
    CHECK_EQ(f.out[1], 0x0D);
    CHECK_EQ(f.out_len, 15);
    CHECK_EQ(f.out[13], 0x01);
    CHECK_EQ(f.out[14], 'C');
}

static void publishing_needs_a_ready_client(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;
    size_t          before;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_publish(&c, "t", "x", 1, false), RES_EIO);
    CHECK_EQ(mqttc_publish(NULL, "t", "x", 1, false), RES_EINVAL);

    bring_up(&c, &f, &s);
    before = f.out_len;

    CHECK_EQ(mqttc_publish(&c, "a/b", "hi", 2, false), RES_OK);
    /* Queued, not yet sent: nothing reaches the socket until the next tick. */
    CHECK_EQ(f.out_len, before);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(f.out_len, before + 9);
    CHECK_EQ(f.out[before], 0x30);

    CHECK_EQ(mqttc_publish(&c, NULL, "x", 1, false), RES_EINVAL);
}

/*
 * Two small publishes in one pass must both be accepted. Reserving the largest
 * imaginable packet for each one meant a bridge could never report telemetry
 * and capabilities together -- the second was refused every single time, and
 * only a real broker showed it.
 */
static void two_small_publishes_in_one_pass_both_fit(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;
    size_t        before;

    bring_up(&c, &f, &s);
    before = f.out_len;

    CHECK_EQ(mqttc_publish(&c, "taiseia101/v1/from/appliance/D/telemetry", "{\"a\":1}", 7,
                           false), RES_OK);
    CHECK_EQ(mqttc_publish(&c, "taiseia101/v1/from/appliance/D/capabilities", "{\"b\":2}", 7,
                           true), RES_OK);

    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK(f.out_len > before + 60);
    CHECK_EQ(mqttc_state(&c), MQTTC_READY);
}

/*
 * Growing a queue here would hide a broker that has stopped reading, until
 * memory ran out. Refusing lets the caller decide.
 */
static void a_busy_send_buffer_refuses_rather_than_grows(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;
    static uint8_t big[MQTT_PAYLOAD_MAX + 8u];

    bring_up(&c, &f, &s);
    f.send_limit = 1;                    /* nothing will drain this tick */

    memset(big, 'x', sizeof(big));
    CHECK_EQ(mqttc_publish(&c, "a/b", big, MQTT_PAYLOAD_MAX, false), RES_OK);
    CHECK_EQ(mqttc_publish(&c, "a/b", big, MQTT_PAYLOAD_MAX, false),
             RES_EBUSY);

    /*
     * Past the ceiling is a different answer from busy: it will never be
     * publishable, and "try again later" would have the caller try forever.
     */
    CHECK_EQ(mqttc_publish(&c, "a/b", big, MQTT_PAYLOAD_MAX + 1u, false),
             RES_ETOOBIG);
}

static void a_peer_that_vanishes_takes_the_client_down(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;

    bring_up(&c, &f, &s);

    f.peer_gone = true;
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void a_packet_out_of_sequence_takes_the_client_down(void)
{
    mqtt_client_t c;
    fake_sock_t   f;
    socket_t      s;

    /* A CONNACK once already connected means we and the broker disagree
     * about where we are. */
    bring_up(&c, &f, &s);
    feed(&f, k_connack_ok, sizeof(k_connack_ok));
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_OK);
    CHECK_EQ(mqttc_state(&c), MQTTC_DOWN);
}

static void bad_arguments_are_refused(void)
{
    mqtt_client_t   c;
    fake_sock_t     f;
    socket_t        s;
    mqttc_options_t o;

    options(&o);
    mqttc_init(&c, &o);
    fake_bind(&f, &s);

    CHECK_EQ(mqttc_tick(NULL, &s, 0), RES_EINVAL);
    CHECK_EQ(mqttc_tick(&c, NULL, 0), RES_EINVAL);

    /* No broker configured is not something to attempt. */
    o.host = "";
    mqttc_init(&c, &o);
    CHECK_EQ(mqttc_tick(&c, &s, 0), RES_EINVAL);

    CHECK(!mqttc_ready(NULL));
    CHECK_EQ(mqttc_state(NULL), MQTTC_DOWN);
    CHECK_EQ(mqttc_last_connack(NULL), 0);
    CHECK_STREQ(mqttc_state_str((mqttc_state_t)99), "unknown");

    /* Defaults fill in for anything left unset. */
    memset(&o, 0, sizeof(o));
    o.host      = "h";
    o.client_id = "c";
    mqttc_init(&c, &o);
    CHECK_EQ(c.opt.port, 1883);
    CHECK_EQ(c.opt.keepalive_s, 60);
    CHECK(c.opt.reconnect_min_ms > 0);
    CHECK(c.opt.reconnect_max_ms >= c.opt.reconnect_min_ms);
}

void test_client(void)
{
    it_connects_subscribes_and_becomes_ready();
    a_refused_connack_takes_the_client_down();
    a_refused_subscription_takes_the_client_down();
    a_client_with_nothing_to_subscribe_to_is_ready_at_connack();
    a_connect_that_never_completes_is_abandoned();
    a_connack_that_never_arrives_is_abandoned();
    a_failing_connect_backs_off_and_caps();
    keepalive_pings_and_gives_up_when_unanswered();
    an_inbound_publish_reaches_the_handler();
    packet_boundaries_do_not_follow_read_boundaries();
    a_partial_send_is_resumed();
    publishing_needs_a_ready_client();
    two_small_publishes_in_one_pass_both_fit();
    a_busy_send_buffer_refuses_rather_than_grows();
    a_peer_that_vanishes_takes_the_client_down();
    a_packet_out_of_sequence_takes_the_client_down();
    bad_arguments_are_refused();
}
