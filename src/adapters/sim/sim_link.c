/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "adapters/sim/sim_link.h"

#include "core/taiseia/datatype.h"
#include "core/taiseia/frame.h"

#include <string.h>

static sim_service_t *find_service(sim_link_t *s, uint8_t id)
{
    size_t i;

    for (i = 0; i < s->n_services; i++) {
        if (s->services[i].service_id == id)
            return &s->services[i];
    }
    return NULL;
}

static void copy_string(char *dst, size_t cap, const char *src)
{
    size_t i;

    for (i = 0; i + 1u < cap && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void sim_link_init(sim_link_t *s, uint16_t sa_id)
{
    if (!s)
        return;

    memset(s, 0, sizeof(*s));
    s->format       = TS_FORMAT_GENERAL;
    s->device_class = TS_CLASS_APPLIANCE;
    s->proto_major  = 1;
    s->proto_minor  = 0;
    s->baud_code    = 3;         /* 9600 bps */
    s->sa_id        = sa_id;
    s->up           = true;

    copy_string(s->make, sizeof(s->make), "SIMULATOR");
    copy_string(s->model, sizeof(s->model), "SIM-0001");
}

result_t sim_link_set_identity(sim_link_t *s, ts_format_t format,
                               uint8_t device_class, uint16_t sa_id,
                               const char *make, const char *model)
{
    if (!s)
        return RES_EINVAL;
    if (format != TS_FORMAT_GENERAL && format != TS_FORMAT_MULTIBYTE)
        return RES_EINVAL;
    if (device_class > 0x0Fu)
        return RES_EINVAL;

    s->format       = format;
    s->device_class = device_class;
    s->sa_id        = sa_id;
    copy_string(s->make, sizeof(s->make), make);
    copy_string(s->model, sizeof(s->model), model);

    return RES_OK;
}

result_t sim_link_add_service(sim_link_t *s, uint8_t service_id, uint8_t dtype,
                              bool writable, uint16_t value,
                              const uint8_t *support, size_t support_len)
{
    sim_service_t *svc;

    if (!s || !support)
        return RES_EINVAL;
    if (service_id > TS_SVC_ID_MAX)
        return RES_EINVAL;
    if (support_len == 0u || support_len > TS_SUPPORT_MAX_BYTES)
        return RES_EINVAL;
    if (s->n_services >= TS_MAX_CAPABILITIES)
        return RES_ETOOBIG;
    if (find_service(s, service_id))
        return RES_EINVAL;

    /* Refuse a support datum the decoder could not accept, so the simulator
     * cannot be configured into a state no appliance could produce. */
    if (s->format == TS_FORMAT_GENERAL) {
        if (support_len != 2u)
            return RES_EINVAL;
    } else {
        const ts_datatype_t *t = ts_datatype(dtype);

        if (!t || t->support_len != support_len)
            return RES_EINVAL;
    }

    svc = &s->services[s->n_services++];
    svc->service_id  = service_id;
    svc->dtype       = dtype;
    svc->writable    = writable;
    svc->value       = value;
    svc->support_len = support_len;
    memcpy(svc->support, support, support_len);

    return RES_OK;
}

result_t sim_link_set_raw_registration(sim_link_t *s, const uint8_t *blob,
                                       size_t len)
{
    if (!s || (!blob && len > 0))
        return RES_EINVAL;
    if (len > sizeof(s->raw))
        return RES_ETOOBIG;

    if (len > 0)
        memcpy(s->raw, blob, len);
    s->raw_len          = len;
    s->raw_registration = true;

    return RES_OK;
}

void sim_link_drop_next(sim_link_t *s, unsigned n)
{
    if (s)
        s->drop_next = n;
}

void sim_link_corrupt_next(sim_link_t *s)
{
    if (s)
        s->corrupt_next = true;
}

void sim_link_set_down(sim_link_t *s)
{
    if (s)
        s->up = false;
}

/* ------------------------------------------------------------ registration */

static size_t put_string(uint8_t *out, size_t pos, const char *s)
{
    size_t n = strlen(s);

    memcpy(&out[pos], s, n);
    out[pos + n] = 0u;

    return n + 1u;
}

/*
 * Assemble a registration payload from the configured identity and services,
 * in the layout the decoder expects. Building the bytes rather than handing
 * over a struct is the point: it is what makes the decoder's test a test.
 */
static result_t build_registration(const sim_link_t *s, uint8_t *out,
                                   size_t cap, size_t *out_len)
{
    size_t pos = 0;
    size_t i;
    size_t need;

    need = TS_REG_HEADER_LEN + strlen(s->make) + 1u + strlen(s->model) + 1u;
    for (i = 0; i < s->n_services; i++) {
        need += (s->format == TS_FORMAT_GENERAL)
                    ? TS_REG_GENERAL_RECORD
                    : 2u + s->services[i].support_len;
    }
    if (need > cap)
        return RES_ETOOBIG;

    out[pos++] = (uint8_t)((s->format == TS_FORMAT_MULTIBYTE ? 0x80u : 0u) |
                           (s->device_class & 0x0Fu));
    out[pos++] = s->proto_major;
    out[pos++] = s->proto_minor;
    out[pos++] = (uint8_t)(s->baud_code & 0x0Fu);   /* fragment 0 */
    out[pos++] = (uint8_t)((s->sa_id >> 8) & 0xFFu);
    out[pos++] = (uint8_t)(s->sa_id & 0xFFu);

    pos += put_string(out, pos, s->make);
    pos += put_string(out, pos, s->model);

    for (i = 0; i < s->n_services; i++) {
        const sim_service_t *svc = &s->services[i];

        out[pos++] = (uint8_t)((svc->service_id & TS_SVC_ID_MASK) |
                               (svc->writable ? TS_SVC_WRITE_BIT : 0u));
        if (s->format == TS_FORMAT_MULTIBYTE)
            out[pos++] = svc->dtype;

        memcpy(&out[pos], svc->support, svc->support_len);
        pos += svc->support_len;
    }

    *out_len = pos;
    return RES_OK;
}

/* ------------------------------------------------------------------ replies */

/*
 * Take a finished packet, apply any pending corruption, and hand it back
 * through the parser. Going out through the encoder and back in through the
 * decoder on every exchange is deliberate: it means the simulator cannot agree
 * with the core about a framing detail that is in fact wrong.
 */
static result_t deliver(sim_link_t *s, uint32_t req_id, uint8_t *pkt,
                        size_t len)
{
    ts_frame_t f;
    result_t   r;

    if (s->corrupt_next) {
        pkt[len - 1u] = (uint8_t)(pkt[len - 1u] ^ 0xFFu);
        s->corrupt_next = false;
    }

    memset(&s->pending, 0, sizeof(s->pending));
    s->pending.id   = req_id;
    s->have_pending = true;

    r = ts_parse_response(pkt, len, &f);
    if (r != RES_OK) {
        /* A damaged packet reaches the caller as a format error, which is
         * exactly what a real receiver would report. */
        s->pending.status = r;
        return RES_OK;
    }

    if (f.data_len > sizeof(s->pending.data)) {
        s->pending.status = RES_ETOOBIG;
        return RES_OK;
    }

    s->pending.status     = RES_OK;
    s->pending.sa_id      = f.sa_id;
    s->pending.service_id = f.service_id;
    s->pending.write      = f.write;
    s->pending.data_len   = f.data_len;
    if (f.data_len > 0)
        memcpy(s->pending.data, f.data, f.data_len);

    return RES_OK;
}

static void fail(sim_link_t *s, uint32_t req_id, result_t status)
{
    memset(&s->pending, 0, sizeof(s->pending));
    s->pending.id     = req_id;
    s->pending.status = status;
    s->have_pending   = true;
}

static result_t reply_registration(sim_link_t *s, uint32_t req_id)
{
    uint8_t  pkt[TS_FRAME_MAX_LEN];
    size_t   payload = 0;
    size_t   len;
    result_t r;

    /* A registration reply has no service byte: length, SA id 0x00, payload,
     * checksum. */
    if (s->raw_registration) {
        if (s->raw_len > sizeof(pkt) - 3u)
            return RES_ETOOBIG;
        if (s->raw_len > 0)
            memcpy(&pkt[2], s->raw, s->raw_len);
        payload = s->raw_len;
    } else {
        r = build_registration(s, &pkt[2], sizeof(pkt) - 3u, &payload);
        if (r != RES_OK)
            return r;
    }

    len = payload + 3u;
    if (len < TS_FRAME_MIN_LEN || len > TS_FRAME_MAX_LEN)
        return RES_ETOOBIG;

    pkt[0]        = (uint8_t)len;
    pkt[1]        = (uint8_t)TS_SA_ID_REGISTRATION;
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);

    return deliver(s, req_id, pkt, len);
}

static result_t reply_service(sim_link_t *s, uint32_t req_id,
                              uint8_t service_id, bool write, uint16_t value)
{
    uint8_t pkt[TS_GENERAL_REQ_LEN];

    pkt[0] = (uint8_t)TS_GENERAL_REQ_LEN;
    pkt[1] = (uint8_t)(s->sa_id & 0xFFu);
    pkt[2] = (uint8_t)((service_id & TS_SVC_ID_MASK) |
                       (write ? TS_SVC_WRITE_BIT : 0u));
    pkt[3] = (uint8_t)((value >> 8) & 0xFFu);
    pkt[4] = (uint8_t)(value & 0xFFu);
    pkt[5] = ts_checksum(pkt, TS_GENERAL_REQ_LEN - 1u);

    return deliver(s, req_id, pkt, TS_GENERAL_REQ_LEN);
}

/* --------------------------------------------------------------------- port */

static result_t sim_submit(void *self, const link_req_t *req)
{
    sim_link_t   *s = (sim_link_t *)self;
    uint8_t       wire[TS_GENERAL_REQ_LEN];
    size_t        wire_len = 0;
    ts_frame_t    in;
    sim_service_t *svc;
    result_t      r;

    if (!s || !req)
        return RES_EINVAL;
    if (!s->up)
        return RES_EIO;
    if (s->have_pending)
        return RES_EBUSY;

    /* Encode the request as bytes and decode it again, so the simulator sees
     * only what a real appliance would see. */
    switch (req->op) {
    case LINK_OP_READ:
        r = ts_build_read(wire, sizeof(wire), &wire_len,
                          req->sa_id, req->service_id);
        break;
    case LINK_OP_REGISTER:
        r = ts_build_read(wire, sizeof(wire), &wire_len,
                          (uint8_t)TS_SA_ID_REGISTRATION, 0u);
        break;
    case LINK_OP_WRITE:
        r = ts_build_write(wire, sizeof(wire), &wire_len,
                           req->sa_id, req->service_id, req->value);
        break;
    default:
        return RES_EINVAL;
    }
    if (r != RES_OK)
        return r;

    r = ts_parse_request(wire, wire_len, &in);
    if (r != RES_OK)
        return r;

    if (s->drop_next > 0) {
        s->drop_next--;
        fail(s, req->id, RES_ETIMEOUT);
        return RES_OK;
    }

    if (req->op == LINK_OP_REGISTER)
        return reply_registration(s, req->id);

    svc = find_service(s, in.service_id);
    if (!svc) {
        fail(s, req->id, RES_ENOTFOUND);
        return RES_OK;
    }

    if (in.write) {
        if (!svc->writable) {
            fail(s, req->id, RES_EUNSUPPORTED);
            return RES_OK;
        }
        svc->value = req->value;
    }

    return reply_service(s, req->id, in.service_id, in.write, svc->value);
}

static result_t sim_poll(void *self, link_resp_t *out)
{
    sim_link_t *s = (sim_link_t *)self;

    if (!s || !out)
        return RES_EINVAL;
    if (!s->have_pending)
        return RES_EAGAIN;

    *out = s->pending;
    s->have_pending = false;

    return RES_OK;
}

static bool sim_is_up(void *self)
{
    const sim_link_t *s = (const sim_link_t *)self;

    return s ? s->up : false;
}

static void sim_close(void *self)
{
    sim_link_t *s = (sim_link_t *)self;

    if (s) {
        s->up           = false;
        s->have_pending = false;
    }
}

static const link_vt_t k_sim_vt = {
    sim_submit,
    sim_poll,
    sim_is_up,
    sim_close
};

void sim_link_bind(sim_link_t *s, link_t *out)
{
    if (!out)
        return;

    out->vt   = &k_sim_vt;
    out->self = s;
}
