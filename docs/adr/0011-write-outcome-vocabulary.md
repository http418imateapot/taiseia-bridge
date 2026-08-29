# ADR 0011 — Write outcomes are our own vocabulary, mapped in configuration

Status: accepted

## Context

The device protocol has no result codes for a write. Success is defined as the
appliance returning a reply **identical to the request**, and the standard says
nothing at all about what a differing reply means, or about how an appliance
should decline a write it cannot perform.

Meanwhile, backends that consume this bridge do have result codes, and they are
specific to whichever system is being spoken to. An earlier system in this
lineage used `0` for written, `240` for out of range and `255` for
unsupported — numbers that appear nowhere in the standard.

Adopting those numbers in the core would be convenient for exactly one backend
and would embed one deployment's API into a project that has no other trace of
it.

## Decision

The core defines its own outcomes, named for what is actually known:

| Outcome | Means |
|---|---|
| `APPL_WRITE_OK` | the reply matched the request byte for byte |
| `APPL_WRITE_UNKNOWN` | the service is not in the capability set |
| `APPL_WRITE_READ_ONLY` | it is, but the appliance declared it read-only |
| `APPL_WRITE_NOT_TAKEN` | the appliance replied with a different value |
| `APPL_WRITE_NO_REPLY` | it timed out, or the transport failed |

Mapping these to whatever numbers a backend expects belongs to the northbound
layer, in **configuration**. A deployment that needs `0` / `240` / `255` writes
that in its config file, where it is a property of that deployment rather than
of this project.

`APPL_WRITE_NOT_TAKEN` is named carefully. Calling it "out of range" would
assert a reason the protocol never gave us — a differing echo is equally
consistent with the appliance rounding a value, refusing it, or being busy. The
name says what was observed and stops there.

The first two outcomes are resolved from the capability set without touching
the link, so a write to a service the appliance never declared costs nothing.

## Consequences

- No deployment's API numbering appears in the source.
- A backend expecting particular numbers needs a mapping table in its config,
  which is one more thing to get right at install time. That is the cost, and
  it falls on the deployment that has the requirement.
- The names describe evidence rather than diagnosis, which makes an operator
  reading a log less likely to chase a cause that was never established.
- If a future revision of the standard defines real write result codes, this
  becomes a translation layer rather than an invention, and the enum is where
  that change lands.

## Alternatives rejected

**Use 0 / 240 / 255 in the core.** Bakes one backend's API into the protocol
layer, and reintroduces exactly the kind of deployment-specific detail this
project was rebuilt to remove.

**Return `result_t` and let the caller interpret.** `RES_EUNSUPPORTED` cannot
distinguish "no such service" from "read-only", and both matter to whoever sent
the command.

**Call a differing echo "out of range".** The most likely explanation, and
still a guess. The protocol gives no basis for it.
