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
topic_telemetry    = {prefix}/from/device/{id}/telemetry
topic_capabilities = {prefix}/from/device/{id}/capabilities
topic_status       = {prefix}/from/device/{id}/status
topic_control      = {prefix}/to/device/{id}/control
topic_ack          = {prefix}/from/device/{id}/control/acknowledgement
```

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

## Alternatives rejected

**A configurable prefix only.** Covers versioning and nothing else. The layout
below the prefix is exactly what differs between deployments.

**Full expression templating.** More placeholders (appliance type, site,
tenant) invites a small language with its own parser and its own bugs. Two
substitutions cover the cases at hand; a third can be added when something
concrete needs it.
