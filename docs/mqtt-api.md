# MQTT API

> Not implemented yet. This is the agreed contract; the northbound side is not
> built. See [architecture.md §8](architecture.md).

## 1. Topics

Five topics, each a configurable template with `{prefix}` and `{id}`. The
defaults:

| Purpose | Default template | Direction |
|---|---|---|
| `topic_telemetry` | `{prefix}/from/device/{id}/telemetry` | published |
| `topic_capabilities` | `{prefix}/from/device/{id}/capabilities` | published, retained |
| `topic_status` | `{prefix}/from/device/{id}/status` | published, retained, also the will |
| `topic_control` | `{prefix}/to/device/{id}/control` | subscribed |
| `topic_ack` | `{prefix}/from/device/{id}/control/acknowledgement` | published |

All five are rendered and checked at startup: each must fit, and no two may be
equal. See [adr/0007](adr/0007-topic-templates.md).

Capabilities and status are retained because a subscriber that joins late needs
them to interpret anything else. Telemetry is not: a retained sample is a stale
sample.

## 2. Values are raw

Payloads carry service identifiers and values as the appliance reports them. No
names, no enumerated strings. Interpreting them needs the per-appliance service
tables, which are not in this repository — see
[adr/0003](adr/0003-no-service-tables.md).

## 3. Telemetry

```jsonc
{
  "device_id":   "...",
  "device_time": "2026-08-29T10:30:00.123Z",
  "sa_id":       4,
  "online":      true,
  "attributes": {
    "0":  1,
    "3":  -3,                 // INT8, sign-extended from two's complement
    "21": 45,                 // a bit type: the raw value, not expanded
    "64": [12, 34, 56],       // a multi-element service
    "5":  "ABC-123"           // a string service
  }
}
```

An attribute value is an **integer, an array of integers, or a string**.
Consumers must tolerate all three; which one appears is determined by the
service's data type, published in capabilities.

Negative values are decoded as signed where the data type says so. Reading
everything as unsigned turns −3 into 65533 with no outward sign of a problem,
which is why it is called out here and tested explicitly.

## 4. Capabilities

```jsonc
{
  "device_id": "...", "device_time": "...", "sa_id": 4, "online": true,
  "capabilities": [
    { "id": 0,  "access": "rw", "dtype": "ENUM16",
      "support": { "kind": "bits",  "mask": 45, "width": 16 } },

    { "id": 3,  "access": "rw", "dtype": "UINT16",
      "support": { "kind": "range", "min": 16, "max": 32 } },

    { "id": 12, "access": "r",  "dtype": "Y-M-D-H-M-S",
      "support": { "kind": "max",   "max": 0 } },

    { "id": 9,  "access": "r",  "dtype": "Y-M-D-H-M-S+UINT32",
      "support": { "kind": "raw",   "hex": "07E80C0C0E1E0000000064" } }
  ]
}
```

`support.kind` is one of four, mirroring the four shapes the standard defines
for support data:

| kind | meaning |
|---|---|
| `range` | `min` and `max` are both meaningful |
| `max` | `max` only; the minimum is fixed at zero |
| `bits` | `mask` — one bit per supported item, `width` bits wide |
| `raw` | a composite type, handed over as hex rather than guessed at |

### 4.1 `bits` means two different things, and `dtype` is how you tell

For `ENUM16`, a set bit at position *i* means **the value *i* is allowed**.

For `ENUM16_Bit`, a set bit at position *i* means **bit *i* of the value is
meaningful**.

Both arrive as a mask and they are not interchangeable. This is why the type
code is published with every capability rather than being resolved away.

## 5. Bit fields are not expanded

A bit-typed value is published as its raw integer. The bridge does not emit
`["cool", "dry"]`, for three reasons:

1. The mapping from bit position to meaning lives in the licensed service
   tables. Emitting names would redistribute them.
2. A capability set of any size expands into a payload several times larger for
   information the consumer already has.
3. `(value >> i) & 1` is one line, and `mask` already says which `i` are worth
   looking at.

For consumers whose rules engine cannot do bit arithmetic, `expand_bitfields`
adds a **sibling** object, leaving `attributes` unchanged in shape:

```jsonc
"attributes":      { "21": 45 },
"attribute_bits":  { "21": [1,0,1,1,0,1,0,0, 0,0,0,0,0,0,0,0] }
```

Default off.

## 6. Control and acknowledgement

```jsonc
// inbound on topic_control
{ "tag": 1, "attributes": { "3": 70 } }
```

```jsonc
// outbound on topic_ack
{ "success": true, "tag": 1, "device_id": "...", "device_time": "...",
  "results": { "3": 0 } }
```

`tag` is echoed back byte for byte, whatever its JSON type, so a consumer may
correlate however it likes.

## 7. Status and offline behaviour

```jsonc
{ "device_id": "...", "device_time": "...", "online": true }
```

The will message omits `device_time`: the broker publishes it at an unknown
later moment, when a timestamp taken now would be misleading.

When the appliance is detached or the link is down, reports continue on
schedule with `"online": false` and an empty attribute set. Going silent would
leave a consumer unable to distinguish a detached appliance from a dead bridge.
