# MQTT API

> Topic rendering, payload construction and the MQTT codec are built and
> tested. The client that opens a socket and drives them is not. See
> [architecture.md §8](architecture.md).

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
    "7": 62,
    "21": 45                  // a bit type: the raw value, not expanded
  }
}
```

`sa_id` is `null` and `attributes` is empty when the appliance is offline.
Reports keep going out on schedule either way: going silent would leave a
consumer unable to tell a detached appliance from a dead bridge.

**Values are unsigned decimal today**, because every service that can currently
be polled uses the general format, which carries no data type — so nothing on
the wire says whether a value is signed. The decoder can sign-extend and is
tested doing so; there is simply nothing yet that tells it to. When multi-byte
polling arrives, its type codes will, and a value may then also be an array or
a string. Consumers should tolerate that widening.

## 4. Capabilities

What can be published here depends on which of the two record formats the
appliance uses, and the difference is larger than it looks. The appliance
declares which at registration.

### 4.1 Multi-byte format — the type code is on the wire

```jsonc
{
  "device_id": "...", "device_time": "...", "sa_id": 224, "online": true,
  "format": "multibyte",
  "capabilities": [
    { "id": 32, "access": "r",  "dtype": "UINT16",
      "support": { "kind": "range", "min": 0, "max": 10000 } },

    { "id": 33, "access": "rw", "dtype": "ENUM16_Bit",
      "support": { "kind": "bits",  "mask": 3845, "width": 16 } },

    { "id": 9,  "access": "r",  "dtype": "Y-M-D-H-M-S+UINT32",
      "support": { "kind": "raw",   "hex": "07E80C0C0E1E0000000064" } }
  ]
}
```

`support.kind` mirrors the shapes the standard defines for support data:

| kind | meaning |
|---|---|
| `range` | `min` and `max` are both meaningful |
| `max` | `max` only; the minimum is fixed at zero |
| `bits` | `mask` — one bit per supported item, `width` bits wide |
| `raw` | hex, when the shape cannot be resolved here |

### 4.2 General format — there is no type code, and it cannot be invented

```jsonc
{
  "device_id": "...", "device_time": "...", "sa_id": 4, "online": true,
  "format": "general",
  "capabilities": [
    { "id": 0, "access": "rw", "support": { "kind": "raw", "hex": "0003" } },
    { "id": 3, "access": "rw", "support": { "kind": "raw", "hex": "1020" } }
  ]
}
```

A general-format record is three bytes: the service byte and **two support
bytes, with no type code**. Those two bytes may be a range, a maximum, or a bit
mask — and nothing on the wire says which. That fact lives in the per-appliance
service tables, which are licensed material and are not in this project.

So for these appliances the bridge publishes the raw bytes and stops. It is a
real reduction in what a consumer gets, and it follows directly from
[adr/0003](adr/0003-no-service-tables.md): a consumer holding the tables loses
nothing, and one without them was never going to be able to interpret a service
identifier anyway.

There is a clean way to recover the richer form for an operator who does hold
the tables — a service-id-to-type map supplied as **configuration**, never
compiled in. Not implemented; noted so that the shape of the answer is on
record.

### 4.3 `bits` means two different things, and `dtype` is how you tell

For `ENUM16`, a set bit at position *i* means **the value *i* is allowed**.

For `ENUM16_Bit`, a set bit at position *i* means **bit *i* of the value is
meaningful**.

Both arrive as a mask and they are not interchangeable. This is why the type
code is published with every capability rather than being resolved away — and
why §4.2 cannot simply pick one of the two.

## 5. Bit fields are not expanded

> `expand_bitfields` is accepted in configuration but does nothing yet: knowing
> which services are bit-typed needs the data type code, which the general
> format does not carry.

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
{ "success": false, "tag": 1, "device_id": "...", "device_time": "...",
  "results": { "3": "ok", "7": "read_only" } }
```

`success` is true only when every item was written.

`tag` is echoed back byte for byte, whatever its JSON type, so a consumer may
correlate however it likes.

The device protocol has **no write result codes at all**: success there is
defined as the appliance replying with a packet identical to the request, and
nothing is said about what a differing reply means. So there is no numbering to
inherit, and this project declines to invent one. The default is the outcome's
name, which needs no lookup table:

| Outcome | Established by | Costs a round trip |
|---|---|---|
| `ok` | the reply matched the request byte for byte | yes |
| `not_taken` | the appliance replied with a different value | yes |
| `read_only` | declared read-only in the capability set | no |
| `unknown` | not in the capability set at all | no |
| `no_reply` | timed out, or the transport failed | — |

A backend that expects particular numbers gets them from configuration, in the
order above:

```
write_result_codes = 0,255,255,240,255
```

which turns the example into `"results": { "3": 0, "7": 255 }`.

`not_taken` deliberately does not claim "out of range". A differing echo is
equally consistent with the appliance rounding the value, refusing it, or being
busy, and the protocol gives no way to tell. See
[adr/0011](adr/0011-write-outcome-vocabulary.md).

## 7. Status and offline behaviour

```jsonc
{ "device_id": "...", "device_time": "...", "online": true }
```

The will message omits `device_time`: the broker publishes it at an unknown
later moment, when a timestamp taken now would be misleading.

When the appliance is detached or the link is down, reports continue on
schedule with `"online": false` and an empty attribute set. Going silent would
leave a consumer unable to distinguish a detached appliance from a dead bridge.
