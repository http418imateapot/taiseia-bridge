# ADR 0006 — MQTT 3.1.1 is written here rather than linked in

Status: accepted

## Context

The northbound side needs an MQTT client. The obvious move is to link
libmosquitto or Paho.

The targets argue against it. An embedded Linux appliance bridge may have no
package manager worth the name; a Windows build would carry a DLL; a future
bare-metal port would carry neither. The subset actually needed is small:
CONNECT, PUBLISH at QoS 0, SUBSCRIBE, PINGREQ, DISCONNECT, and a last will.

## Decision

Write the client. Keep the codec — packet encode and decode — in the core as
pure functions over caller-supplied buffers, with the socket behind a port.

Scope is deliberately fixed at QoS 0 with a clean session. Nothing in the
payload contract needs delivery guarantees that the reporting interval does not
already provide: a lost report is superseded within one interval, and a control
command is acknowledged on its own topic.

## Consequences

- No run-time dependency, and the same code on every target.
- The codec is unit-testable without a broker, which is where most of the risk
  in a hand-written protocol implementation actually lives.
- We own the bugs. A library has had more eyes on it than this ever will.
- QoS 1 and 2, and session persistence, are not available. If a deployment
  needs them, this decision is the thing to revisit first — the codec would
  need a packet identifier and a retransmission store.

## Alternatives rejected

**libmosquitto.** Mature and well tested, and a package dependency on every
target plus a licence to track.

**Implement QoS 1 as well, for safety.** Delivery guarantees that duplicate
what periodic reporting already gives, in exchange for retransmission state
that has to be correct on a device that may lose power at any moment.
