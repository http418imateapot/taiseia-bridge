# ADR 0012 — Fragmented replies are rejoined below the port, and the registration continuation format is an inference

Status: accepted

## Context

A reply too long for one packet is sent as several. Each is a complete,
checksummed packet carrying a fragment number; the numbers count down and zero
marks the last. Only the payloads join up.

The standard settles this for a **multi-byte** reply. Every fragment repeats the
same header, carries its own fragment number at bytes 4 and 5, and states the
count of the items *in that fragment*. Concatenating the data fields and summing
the counts is the whole of it.

It does not settle it for a **registration** reply. There, the fragment number
lives inside the payload, at a fixed offset within the six-byte header — which
means a continuation cannot be a pure continuation, or the number could not be
read at all. The standard says what the field is and where it sits. It does not
say what the rest of a continuation contains.

Two readings are self-consistent with the field's placement:

1. Every fragment repeats the fixed header, and what follows it joins up.
2. A continuation carries the header bytes as filler and the payload begins at
   some other offset the standard does not name.

Only the first requires nothing that is not written down.

A third question is where the joining happens. The framing code is pure and the
appliance model is a state machine over `submit`/`poll`; the fragment boundary
could live in either, or in the adapter between them.

## Decision

Rejoin fragments in `src/core/taiseia/reassembly.c`, and call it from the link
adapters. Everything above the appliance link port sees one whole reply or an
error — never a piece.

Implement reading 1 for registration, mark it as an inference in the header, in
`docs/architecture.md`, and here. On completion, clear the fragment nibble in
the assembled header, because what is handed over is whole; `ts_registration_parse`
still refuses a lone fragment, which protects any caller that does not
reassemble.

Refuse rather than assemble something plausible. A fragment number that does not
count down, a service or data type that changes mid-stream, a reply past the
buffer, a continuation shorter than the header it must repeat — all are errors.
Never truncate: a short batch of readings looks exactly like a complete one.

Give each further fragment its own 200 ms response deadline in the adapter. The
standard gives one to each reply; a single deadline covering a long answer would
cut off a legitimate one.

## Consequences

The appliance model and everything above it are unchanged by fragmentation:
there is no partial state to hold, no half-decoded registration, no reply that
is "still arriving". That is what makes the model testable by passing numbers.

`link_resp_t` now carries a reassembly-sized buffer rather than a packet-sized
one, so it is about a kilobyte and is copied on every `poll`. On a workstation
this is nothing; on a microcontroller it is the single largest object the port
imposes, and `TS_REASSEMBLY_MAX` is the knob.

The cost is the honest one: **the registration continuation format is a guess**,
and a wrong guess is not loud. It would misplace bytes and produce a capability
list that decodes into plausible nonsense. Only a real appliance that sends a
capability list longer than one packet can settle it, and none has been
connected to this code. The refusals above bound the damage — a mis-sliced
continuation usually fails the header check or the record-length arithmetic —
but they do not eliminate it.

Multi-byte fragmentation carries no such doubt.

## Alternatives rejected

**Refuse fragmented replies, as before.** Correct, and it made meters and
sensors — whose readings are exactly what does not fit in one packet —
unpollable. The gap was worth closing even at the price of one inference.

**Reassemble inside the appliance model.** It already holds the retry and
re-registration state, so this looked like the same kind of work. It is not: the
model would then need a fragment-shaped intermediate state, and every test of
scheduling would have to construct one. Fragmentation is a transport concern.

**Reassemble inside each adapter.** The UART adapter and any future one would
each carry the same buffer and the same rules, and would eventually disagree
about them — the same argument that keeps retries out of the adapters
([ADR 0001](0001-appliance-link-is-a-port.md)).

**Pick reading 2, or guess an offset.** It requires inventing a number the
standard does not contain. Reading 1 requires nothing.

**Accept a reassembled registration payload whose header still says "fragment
3".** Simpler by one line, and it would leave `ts_registration_parse` unable to
refuse a lone fragment — a guard that matters precisely for callers who do not
reassemble.
