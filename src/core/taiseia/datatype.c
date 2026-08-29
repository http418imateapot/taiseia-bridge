/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/taiseia/datatype.h"

/* A composite value is a six-byte timestamp followed by a scalar. */
#define DT_STAMP 6u

/*
 * NOTE on INT64 (code 0x33). The published table gives its value length as 4
 * bytes and its support length as 8, which cannot be right for a 64-bit
 * integer and disagrees with UINT64 (0x23) two rows above, listed as 8 and 16.
 * We implement 8 and 16. An appliance that really did send four bytes here
 * would fail the length check in the capability decoder, which is the outcome
 * we want: refused and visible, rather than silently misread.
 */
static const ts_datatype_t k_types[] = {
    { 0x01, "ENUM16",     2, 2,  TS_SUPPORT_BITS,      false },
    { 0x10, "ENUM16_Bit", 2, 2,  TS_SUPPORT_BITS,      false },

    { 0x20, "UINT8",      1, 2,  TS_SUPPORT_RANGE,     false },
    { 0x21, "UINT16",     2, 4,  TS_SUPPORT_RANGE,     false },
    { 0x22, "UINT32",     4, 8,  TS_SUPPORT_RANGE,     false },
    { 0x23, "UINT64",     8, 16, TS_SUPPORT_RANGE,     false },

    { 0x30, "INT8",       1, 2,  TS_SUPPORT_RANGE,     true  },
    { 0x31, "INT16",      2, 4,  TS_SUPPORT_RANGE,     true  },
    { 0x32, "INT32",      4, 8,  TS_SUPPORT_RANGE,     true  },
    { 0x33, "INT64",      8, 16, TS_SUPPORT_RANGE,     true  },

    { 0x40, "Y-M",        2, 2,  TS_SUPPORT_MAX,       false },
    { 0x41, "M-D",        2, 2,  TS_SUPPORT_MAX,       false },
    { 0x42, "H-M",        2, 2,  TS_SUPPORT_MAX,       false },
    { 0x43, "M-S",        2, 2,  TS_SUPPORT_MAX,       false },
    { 0x44, "Y-M-D-H-M-S", 6, 6, TS_SUPPORT_MAX,       false },
    { 0x45, "Y-M-D-H-M",   5, 5, TS_SUPPORT_MAX,       false },

    { 0x50, "STRING",     0, 0,  TS_SUPPORT_VARIABLE,  false },

    { 0x60, "Y-M-D-H-M-S+INT8",   DT_STAMP + 1u, DT_STAMP + 2u,
      TS_SUPPORT_COMPOSITE, true  },
    { 0x61, "Y-M-D-H-M-S+INT16",  DT_STAMP + 2u, DT_STAMP + 4u,
      TS_SUPPORT_COMPOSITE, true  },
    { 0x62, "Y-M-D-H-M-S+INT32",  DT_STAMP + 4u, DT_STAMP + 8u,
      TS_SUPPORT_COMPOSITE, true  },
    { 0x63, "Y-M-D-H-M-S+INT64",  DT_STAMP + 8u, DT_STAMP + 16u,
      TS_SUPPORT_COMPOSITE, true  },

    { 0x70, "Y-M-D-H-M-S+UINT8",  DT_STAMP + 1u, DT_STAMP + 2u,
      TS_SUPPORT_COMPOSITE, false },
    { 0x71, "Y-M-D-H-M-S+UINT16", DT_STAMP + 2u, DT_STAMP + 4u,
      TS_SUPPORT_COMPOSITE, false },
    { 0x72, "Y-M-D-H-M-S+UINT32", DT_STAMP + 4u, DT_STAMP + 8u,
      TS_SUPPORT_COMPOSITE, false },
    { 0x73, "Y-M-D-H-M-S+UINT64", DT_STAMP + 8u, DT_STAMP + 16u,
      TS_SUPPORT_COMPOSITE, false }
};

#define N_TYPES (sizeof(k_types) / sizeof(k_types[0]))

const ts_datatype_t *ts_datatype(uint8_t code)
{
    size_t i;

    for (i = 0; i < N_TYPES; i++) {
        if (k_types[i].code == code)
            return &k_types[i];
    }
    return NULL;
}

const char *ts_datatype_name(uint8_t code)
{
    const ts_datatype_t *t = ts_datatype(code);

    return t ? t->name : "unknown";
}

size_t ts_datatype_count(void)
{
    return N_TYPES;
}

const ts_datatype_t *ts_datatype_at(size_t i)
{
    return (i < N_TYPES) ? &k_types[i] : NULL;
}

result_t ts_decode_int(const uint8_t *data, size_t n, bool is_signed,
                       int64_t *out)
{
    uint64_t v = 0;
    size_t   i;

    if (!data || !out)
        return RES_EINVAL;
    if (n != 1u && n != 2u && n != 4u && n != 8u)
        return RES_EINVAL;

    for (i = 0; i < n; i++)
        v = (v << 8) | (uint64_t)data[i];

    if (is_signed && n < 8u) {
        /* Sign-extend from the top bit of the narrowest containing width.
         * Done on the unsigned value so the shift is always defined. */
        uint64_t sign_bit = (uint64_t)1 << ((n * 8u) - 1u);

        if (v & sign_bit) {
            uint64_t mask = ~(uint64_t)0 << (n * 8u);

            v |= mask;
        }
    }

    *out = (int64_t)v;
    return RES_OK;
}
