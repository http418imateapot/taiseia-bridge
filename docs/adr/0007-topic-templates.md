# ADR 0007 — Topics are configurable templates, validated at startup

Status: accepted

## Context

Every deployment has its own topic conventions, and a bridge that hard-codes
one is useful in exactly one place. Making only the prefix configurable is the
usual half-measure: it lets an operator change `v3` to `v4` and nothing else.

There is a second, unrelated problem. A topic built at publish time can turn
out to be too long for its buffer at publish time, which is a poor moment to
discover it — the device is running, the report is dropped, and the failure
recurs every interval.

## Decision

Each of the five topics is a template with `{prefix}` and `{id}` placeholders,
set in configuration:

```
topic_prefix       = taiseia101/v1
topic_telemetry    = {prefix}/from/appliance/{id}/telemetry
topic_capabilities = {prefix}/from/appliance/{id}/capabilities
topic_status       = {prefix}/from/appliance/{id}/status
topic_command      = {prefix}/to/appliance/{id}/command
topic_ack          = {prefix}/from/appliance/{id}/command/ack
```

### Why the version sits under the protocol name

`taiseia101/v1`, not `v1/taiseia101`. The test is what `v1` versions.

It versions **this bridge's payload schema**, not a system-wide contract: the
envelope carries `sa_id` and an `attributes` object keyed by raw TaiSEIA
service identifiers ([ADR 0003](0003-no-service-tables.md)). That is not a
protocol-neutral shape that a Modbus or BACnet bridge on the same broker would
also emit. A version that only constrains one bridge belongs beside the thing
it constrains.

Two consequences follow, and both are why it matters:

- An ACL granting `taiseia101/#` survives a bump to `v2`. One written as
  `v1/taiseia101/#` has to be edited.
- In a broker browser, `taiseia101/` stays one subtree with versions inside it,
  rather than scattering across `v1/` and `v2/`.

Version-first would be right if `v1` named a platform-wide envelope that every
bridge emitted. It does not, so it is not.

### Why the segments are named as they are

`appliance`, not `device` or `HNA`. A topic level should say what is being
addressed, and what a subscriber addresses is an appliance. `device` is generic
to the point of carrying nothing. `HNA` names the *sender's* role from the
standard -- and the MQTT side is precisely where this program is not a
conforming HNA, since a real one speaks the standard's own transport
northbound. Putting `HNA` in a topic would assert the one thing this project
takes care never to assert.

`command`, not `control`: these are discrete, correlated messages with a `tag`
and a one-shot result, not a continuous channel. `write` was the runner-up and
is more precise about what happens today, but it would misname the first
command that is not a write.

`command/ack`, not `control/acknowledgement`: `ack` needs no explanation and
costs twelve fewer bytes of the topic budget. An interior node with no
publisher of its own is ordinary in MQTT.

Rendering is a pure function. At startup, with the real device identifier in
hand, all five are rendered and checked: each must fit, and no two may be
equal. Failing either, the program refuses to start.

## Consequences

- Any broker layout can be accommodated without a code change.
- A misconfiguration is a startup failure with a message, rather than silence
  once per interval.
- Rejecting duplicate templates catches the copy-paste error that would
  otherwise publish telemetry and capabilities to the same topic, where the
  symptom is a consumer seeing alternating payload shapes.
- Templates are not validated against MQTT wildcard rules. An operator can
  still write something a broker will reject at SUBSCRIBE time.
- A rendered topic gets `TOPIC_MAX` (160) bytes. With the defaults that leaves
  119 for the device identifier -- comfortably above the 23 bytes an MQTT 3.1.1
  server is *required* to accept in a client identifier, which is the real
  ceiling. `CFG_PREFIX_MAX` is 64 rather than 160's share of it, because the
  prefix is where a deployment scopes itself and 32 ran out before the scoping
  did.
- Too long and malformed are reported differently. They have different cures --
  shorten the identifier, or fix the placeholder -- and one message for both
  leaves an operator guessing.

## Alternatives rejected

**A configurable prefix only.** Covers versioning and nothing else. The layout
below the prefix is exactly what differs between deployments.

**Hard-coding `TaiSEIA101` as the root.** Self-describing on a broker carrying
several protocols, which is a real benefit -- and available today by setting
`topic_prefix`. As a *default* it spends the version slot on a name, puts an
unaffiliated project's output under a standards body's namespace where it reads
as an endorsement, and gives every deployment everywhere the same root to
collide in.

**Full expression templating.** More placeholders (appliance type, site,
tenant) invites a small language with its own parser and its own bugs. Two
substitutions cover the cases at hand; a third can be added when something
concrete needs it.
