/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "adapters/sim/sim_link.h"

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

void sim_link_init(sim_link_t *s, uint8_t sa_id)
{
    if (!s)
        return;

    memset(s, 0, sizeof(*s));
    s->sa_id = sa_id;
    s->up    = true;
}

result_t sim_link_add_service(sim_link_t *s, uint8_t service_id, uint8_t dtype,
                              bool writable, uint16_t value,
                              uint16_t support_hi, uint16_t support_lo)
{
    sim_service_t *svc;

    if (!s)
        return RES_EINVAL;
    if (service_id > TS_SVC_ID_MAX)
        return RES_EINVAL;
    if (s->n_services >= TS_MAX_CAPABILITIES)
        return RES_ETOOBIG;
    if (find_service(s, service_id))
        return RES_EINVAL;

    svc = &s->services[s->n_services++];
    svc->service_id = service_id;
    svc->dtype      = dtype;
    svc->writable   = writable;
    svc->value      = value;
    svc->support_hi = support_hi;
    svc->support_lo = support_lo;

    return RES_OK;
}

result_t sim_link_set_registration(sim_link_t *s, const uint8_t *blob,
                                   size_t len)
{
    if (!s || (!blob && len > 0))
        return RES_EINVAL;
    if (len > sizeof(s->reg_blob))
        return RES_ETOOBIG;

    if (len > 0)
        memcpy(s->reg_blob, blob, len);
    s->reg_blob_len = len;

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

/*
 * Assemble a reply packet the way an appliance would, then hand it back
 * through the parser. Going out through the encoder and back in through the
 * decoder on every exchange is deliberate: it means the simulator cannot agree
 * with the core about a framing detail that is in fact wrong.
 */
static result_t emit(sim_link_t *s, uint32_t req_id, uint8_t service_id,
                     bool write, const uint8_t *data, size_t data_len)
{
    uint8_t    pkt[TS_FRAME_MAX_LEN];
    size_t     len = TS_FRAME_OVERHEAD + data_len;
    ts_frame_t f;
    result_t   r;

    if (len < TS_FRAME_MIN_LEN || len > TS_FRAME_MAX_LEN)
        return RES_ETOOBIG;

    pkt[0] = (uint8_t)len;
    pkt[1] = s->sa_id;
    pkt[2] = (uint8_t)((service_id & TS_SVC_ID_MASK) |
                       (write ? TS_SVC_WRITE_BIT : 0u));
    if (data_len > 0)
        memcpy(&pkt[3], data, data_len);
    pkt[len - 1u] = ts_checksum(pkt, len - 1u);

    if (s->corrupt_next) {
        pkt[len - 1u] = (uint8_t)(pkt[len - 1u] ^ 0xFFu);
        s->corrupt_next = false;
    }

    memset(&s->pending, 0, sizeof(s->pending));
    s->pending.id = req_id;

    r = ts_parse(pkt, len, &f);
    if (r != RES_OK) {
        /* A damaged packet reaches the caller as a format error, which is
         * exactly what a real receiver would report. */
        s->pending.status = r;
        s->have_pending   = true;
        return RES_OK;
    }

    s->pending.status     = RES_OK;
    s->pending.sa_id      = f.sa_id;
    s->pending.service_id = f.service_id;
    s->pending.write      = f.write;
    s->pending.data_len   = f.data_len;
    if (f.data_len > 0)
        memcpy(s->pending.data, f.data, f.data_len);

    s->have_pending = true;
    return RES_OK;
}

static result_t sim_submit(void *self, const link_req_t *req)
{
    sim_link_t *s = (sim_link_t *)self;
    uint8_t     wire[TS_GENERAL_REQ_LEN];
    size_t      wire_len = 0;
    ts_frame_t  in;
    sim_service_t *svc;
    uint8_t     data[2];
    result_t    r;

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
    case LINK_OP_REGISTER:
        r = ts_build_read(wire, sizeof(wire), &wire_len,
                          (req->op == LINK_OP_REGISTER)
                              ? (uint8_t)TS_SA_ID_REGISTRATION
                              : req->sa_id,
                          req->service_id);
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

    r = ts_parse(wire, wire_len, &in);
    if (r != RES_OK)
        return r;

    if (s->drop_next > 0) {
        s->drop_next--;
        memset(&s->pending, 0, sizeof(s->pending));
        s->pending.id     = req->id;
        s->pending.status = RES_ETIMEOUT;
        s->have_pending   = true;
        return RES_OK;
    }

    if (req->op == LINK_OP_REGISTER)
        return emit(s, req->id, 0u, false, s->reg_blob, s->reg_blob_len);

    svc = find_service(s, in.service_id);
    if (!svc) {
        memset(&s->pending, 0, sizeof(s->pending));
        s->pending.id     = req->id;
        s->pending.status = RES_ENOTFOUND;
        s->have_pending   = true;
        return RES_OK;
    }

    if (in.write) {
        if (!svc->writable) {
            memset(&s->pending, 0, sizeof(s->pending));
            s->pending.id     = req->id;
            s->pending.status = RES_EUNSUPPORTED;
            s->have_pending   = true;
            return RES_OK;
        }
        svc->value = req->value;
    }

    data[0] = (uint8_t)((svc->value >> 8) & 0xFFu);
    data[1] = (uint8_t)(svc->value & 0xFFu);

    return emit(s, req->id, in.service_id, in.write, data, sizeof(data));
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
