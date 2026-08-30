# ADR 0014 — A general-format value may be typed from operator-supplied metadata

Status: proposed

Nothing in this ADR is implemented. It records a design that
[ADR 0003](0003-no-service-tables.md) already anticipated and that
[lab/](../../lab/README.md) has now quantified, so that the decision can be taken
or refused deliberately rather than drifted into.

## Context

A general-format appliance answers a read with two bytes and nothing that says
what they are. The registration record for such a service carries the service
byte and two support bytes — no type code. ADR 0003 decided not to ship the
per-appliance service tables, so this program publishes the two bytes as an
unsigned 16-bit number and hands the question on. ADR 0003's final consequence
already names the way out: an operator who holds the tables can supply a
service-id-to-type map as configuration, which keeps the licensed part on the
operator's side of the boundary.

`lab/` has since measured what is being handed on. Across the ten types the field
can carry, two conforming sender layouts and four plausible receiver readings
give 80 combinations, of which 2 misread — both `INT8`, 128 values each. The
services a demand-response consumer most needs to read correctly are one-byte
signed temperatures, so the affected type is not an obscure one.

Two further facts bear on the decision. A `lenient` reading is the only one of
the four that is exact — it accepts a byte pair if and only if some conforming
sender emits it — so a receiver can close the ambiguity unilaterally without
accepting traffic no appliance produces. And a wrong entry in the operator's map
produces a *legal wrong value*: 253 instead of −3, with a valid length and a
valid checksum, and nothing to notice.

## Decision

*(Proposed.)*

Add a lookup port whose only obligation is `(SA id, service id) → type code`,
with adapters supplying it — a configuration file for the operator's transcribed
map, and, where a format does carry a type code, the registration record. Add a
pure codec in `src/core` taking the layout and the reading as parameters, with
all four readings present in the code rather than one of them inlined.

Ship `lenient` as the receiving default, on the strength of the exactness result.

**Default off.** With no map configured, output is byte-for-byte what it is
today, and the existing tests are the regression guard for that. Typing a value
is something an operator turns on with a file they wrote.

**Refuse bad rows at load.** An unknown or too-wide type code, a service id using
the write bit, a duplicate, a full table — any of them refuses startup rather
than being skipped. A map that half-loaded is worse than one that did not.

**Publish the provenance.** A consumer must be able to tell a value typed from an
operator's file from one the wire described.

## Consequences

- Values become correct instead of raw for operators who hold the tables, without
  a single service table entering this repository.
- The licensed material stays on the operator's side of the boundary, which is
  where ADR 0003 put it.
- **A misconfigured map produces plausible wrong numbers.** Refusing bad rows at
  load catches the malformed, not the mistaken: a row naming `UINT8` where the
  appliance means `INT8` loads cleanly and misreads every negative value. This is
  a real cost, it is not fully mitigable, and it is the strongest argument
  against this ADR.
- Four readings in the code where one would do, kept because the matrix that
  justifies the default is only checkable if the alternatives exist.
- The write path raises the same question a layer up — see D1 in the
  [audit](../../lab/docs/oracle-audit.md) — and this ADR does not settle it. The
  command value is `uint16_t` end to end today.

## Alternatives rejected

**Leave it as it is.** Defensible, and it is the current position. The cost is
that the ambiguity does not go away; it moves to whoever decodes downstream, with
less context and no record that a choice is being made.

**Guess the type from the support bytes.** Reading the support datum requires
knowing the type — a range, a maximum and a mask are the same two bytes. It is
the type question again wearing a hat.

**Ship the service tables.** ADR 0003, and the licence.

**Pick a layout instead of a reading.** P1 and P2 in
[lab/docs/method.md](../../lab/docs/method.md) say no layout is safe against both
strict readings. A sender cannot close this; only a receiver can.
