# ADR 0002 — The appliance side is CNS 16014 §7, in the HNA role

Status: accepted

## Context

The standard defines two application protocols. §6 is the network protocol
spoken between a cloud system or home gateway and a home network adaptor. §7 is
the device monitoring protocol spoken between that adaptor and the appliance
itself, over a point-to-point UART.

A bridge that wants to reach the appliance directly has to speak §7 and occupy
the adaptor's role. There is no other interface to the appliance in the
standard — in particular there is no Modbus profile anywhere in it.

## Decision

Implement §7 and take the HNA role: we are the requester, the appliance
answers, and we never initiate on the appliance's behalf.

Northbound we speak MQTT rather than §6. The consequence is stated plainly
wherever it matters: this is **not** a conforming HNA and no conformance claim
is made.

Timing and retry policy come from the standard and live in
`core/taiseia/limits.h` — a 200 ms response deadline, at most three retries,
and a 30 ms inter-byte ceiling.

## Consequences

- The appliance link is simple to reason about: one outstanding request, one
  reply, no unsolicited traffic to demultiplex.
- Because we are never the responder, no part of this program has a hard
  real-time deadline. That is what allows a desktop operating system and a
  USB-to-UART adapter to be a viable host at all.
- We inherit the standard's ceiling of one appliance per link. Serving several
  appliances means several links, not a bus.
- Replacing MQTT with a real §6 implementation later is a northbound change
  only; nothing southbound depends on the choice.

## Alternatives rejected

**Implement §6 as well, and be a conforming HNA.** Larger, and it buys nothing
for the deployments this project targets, which have an MQTT broker and no
TaiSEIA-speaking gateway. Left open by the layering.

**Talk to a vendor gateway that re-exposes the appliance over Modbus.** This is
what an earlier generation of this kind of device did. It works, but it binds
the project to one vendor's register layout, which is neither standardised nor
publishable. See [0003](0003-no-service-tables.md) for the related boundary.
