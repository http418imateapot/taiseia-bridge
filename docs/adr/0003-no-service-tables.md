# ADR 0003 — No service tables; capabilities are queried at run time

Status: accepted

## Context

Every service an appliance offers has an identifier, a data type, an access
direction and a permitted range or set. Collectively these fill roughly sixty
pages of the standard's Annex A, one table per appliance type.

A bridge could embed a copy of those tables and publish friendly names and
enumerated strings. Two things argue against it.

The first is legal. The standard is sold per seat and each copy is watermarked
with the purchaser's account. Reproducing its tables — as a header, as data, or
as a comment — is redistribution of licensed material, whatever form it takes.

The second is operational, and would apply even if the tables were public: a
service identifier added to the standard would require a firmware change on
every deployed unit before that service could be reported at all.

## Decision

Ship no service tables. Publish raw service identifiers and raw values.

Capabilities are obtained from the appliance itself through the registration
exchange, which reports, per service: the access direction, the data type code,
and the support data — a range, a maximum, or a mask. The appliance is the
authority on what it can do.

The one table we do implement is the data type code map in
`core/taiseia/datatype.c`: code to byte width and support-data shape. It is a
format-layer fact needed to split bytes off a wire and carries no per-appliance
semantics.

Consumers interpret meaning using their own licensed copy.

## Consequences

- A new appliance type needs no change here at all.
- Published capabilities are *richer* than a static table would give, not
  poorer: the values come from the device in front of you rather than from a
  document describing a class of devices.
- The type code must be published alongside each capability, because the
  support mask means different things for different types — for an enumeration
  it says which values are allowed, for a bit type which bits are meaningful.
  Conflating the two produces a plausible, wrong reading.
- Consumers need the standard to do anything semantic. This is a real cost and
  is stated in the README rather than hidden.

## Alternatives rejected

**Embed the tables.** Redistributes licensed material, and pins the firmware to
a revision of the standard.

**Ship the tables as a separate optional data file.** Moves the redistribution
one directory across without changing what it is.

**Publish names but not ranges.** The names are the licensed part. This gets
the legal exposure and loses the range information as well.
