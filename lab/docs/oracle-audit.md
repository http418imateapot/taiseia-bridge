# Where this project's expected values come from

A test compares an output against an expected value. Everything the test can
establish rests on that expected value being right, and nothing in the test can
check it. Where the expected value was transcribed by a person from prose, the
test inherits whatever that person decided — including, silently, any place the
prose allowed more than one decision.

This is an inventory of those places. It is not a defect list. Most entries are
decisions that were made carefully and written down; the point of collecting
them is that they are *premises of the test suite*, and a premise nobody has
listed is a premise nobody re-examines.

Each entry says what the standard settles, what it leaves open, what this
project chose, which tests rest on the choice, and whether the choice can be
made a parameter and swept.

## The failure mode is not hypothetical here

Three commits in the recent history are the same shape — an assertion that
passed, or would have passed, without establishing anything:

| Commit | What the test asserted | Why it did not hold |
|---|---|---|
| `c20ba30` | fragment length is `len - 1` | the transcription was wrong; every parser refuses such a packet, and CI caught it only because the parser is stricter than the test |
| `d87aef4` | simulator exchanges succeed | request structs were uninitialised; Linux happened to zero the stack, MSVC filled it with `0xCC`. The tests passed on one platform for a reason unrelated to the property |
| `b5b54b1` | a value reads back | two checks named no service and inherited one from the previous request, so they read back a different service than the one written |

Two of the three were caught by a *second compiler*, not by the assertions. The
third was caught by the parser being stricter than the test that fed it. None
was caught by the test suite reasoning about itself, because a test suite cannot:
it has only the oracle it was given.

## Class A — the standard permits more than one reading

These are the entries that matter. Each is a place where two implementations can
both follow the text and disagree on the wire.

### A1. Registration continuation format

- **Settled**: the fragment number's position and meaning, inside the payload at
  a fixed offset.
- **Open**: what the rest of a continuation fragment contains. The number's
  placement rules out a pure payload continuation — the number could not be read
  — but does not say what replaces it.
- **Chosen**: every fragment repeats the fixed header and what follows joins up.
  The only reading that needs nothing the standard does not state.
- **Marked**: `src/core/taiseia/reassembly.h`, `docs/architecture.md`,
  [ADR 0012](../../docs/adr/0012-fragment-reassembly.md). Three places, which is
  the standard the rest of this inventory should be held to.
- **Tests resting on it**: `tests/test_reassembly.c`, `tests/test_registration.c`.
- **Sweepable**: yes, in principle — the two readings differ in where the joined
  payload starts, which is arithmetic. Not yet done; it needs a packet-level
  model rather than a byte-pair one.
- **Confirmable**: only by a real appliance.

### A2. The general format's two-byte data field

- **Settled**: the field is two bytes; negatives are two's complement; a one-byte
  value goes in the low byte with the high byte zero.
- **Open**: which of those last two applies when the value is one byte wide *and*
  negative — they give different bytes for the same value — and, before that,
  what type the field holds at all, since the general format carries no type code
  and the registration record carries only support data.
- **Chosen**: on the published path, nothing is decoded. The field goes out as an
  unsigned 16-bit number and the type question is handed to the consumer, who has
  the service tables. See `src/core/model/appliance.h:87`,
  `src/core/mqtt/payload.c:76`, `src/apps/cli/main.c:150`, and
  [ADR 0003](../../docs/adr/0003-no-service-tables.md).
- **Also now implemented, and called by nothing**:
  `src/core/taiseia/general.c` decodes and encodes the field given a type, with
  the layout and the reading as parameters and all four readings present. It
  changes no behaviour; wiring it up is
  [ADR 0014](../../docs/adr/0014-typed-general-data-field.md), still proposed.
  It is checked exhaustively by `tests/test_general.c` and, for arbitrary
  inputs, by `lab/proofs/` — see [proofs.md](proofs.md).
- **Tests resting on it**: `tests/test_payload.c`, `tests/test_appliance.c`.
- **Sweepable**: yes, and done — see [method.md](method.md) and
  `lab/results/matrix.json`. Of 80 type × layout × reading combinations, 2
  misread, both `INT8`, 128 values each. No layout is safe against both strict
  readings, so a sender cannot close this alone; a `lenient` receiver is the only
  unilateral fix, and is the only one of the four readings that is exact.
- **Note on the current position**: deferring is defensible and is what ADR 0003
  decided. It is worth being clear about what it defers, though. An appliance
  reporting −3 for an `INT8` service publishes as `65533` or as `253` depending on
  which layout its firmware picked, and neither is −3. The consumer that decodes
  downstream meets these 80 cells whether or not this program does.

### A3. Differing write echo

- **Settled**: a successful write is answered by a reply identical to the request.
- **Open**: what a differing reply means. The protocol has no result codes.
- **Chosen**: report `APPL_WRITE_NOT_TAKEN` and do not guess at a reason.
  `src/core/model/appliance.h:50`, `src/core/model/appliance.c:472`,
  [ADR 0011](../../docs/adr/0011-write-outcome-vocabulary.md).
- **Tests resting on it**: `tests/test_appliance.c`.
- **Sweepable**: no. This is a vocabulary choice, not an arithmetic one. It
  belongs in the inventory anyway, because it is an expected value a person chose
  and the tests now encode.

## Class B — the published table is internally inconsistent

### B1. `INT64` (code `0x33`) lengths

- The published lengths disagree with `UINT64` two rows above and cannot both be
  right for a 64-bit integer. Implemented as 8 and 16, with a `NOTE` in
  `src/core/taiseia/datatype.c` saying so.
- Eight bytes wide, so it is not carryable in the general format and is outside
  the matrix. It reaches the multi-byte decoder instead.
- **Documented**: yes.

### B2. `INT16` (code `0x31`) value length

- Implemented as 2. There is a published inconsistency here of the same kind as
  B1 — the printed data length disagrees with the type's own name and with the
  worked layout elsewhere in the annex.
- **Documented**: no. `src/core/taiseia/datatype.c:28` is a plain row.
- **And it is the judgement that moves the answer.** `lab/oracle/sensitivity.py`
  flips each carryable type's width to the other plausible value and re-runs the
  matrix. Two types are sensitive, and only one of them is disputed:

```
0x30 INT8    1 -> 2 bytes: 2 misreading cells -> 0
0x31 INT16   2 -> 1 bytes: 0 misreading cells -> 2   (A x strict, B x wide)
```

  Taking `INT16` as one byte would make it a second signed one-byte type and
  double the reach of the ambiguity in A2 — from one type to two. The other eight
  carryable types move only an exactness verdict, and only for the `low` reading.
- **Recommended**: give `INT16` a `NOTE` like `INT64`'s. It is a premise of the
  test suite rather than a copied fact, and it is currently the only load-bearing
  one that is silent.

## Class C — our policy, not the standard's

Listed so they are not mistaken for Class A. These are ceilings and vocabularies
this project chose; they can be changed by deciding to, and no appliance
disagrees with them.

`TS_REASSEMBLY_MAX`, `TS_MAX_CAPABILITIES`, `TS_VALUE_MAX_BYTES` (`limits.h`,
each marked "ours, not the protocol's"), the `appl_write_result_t` vocabulary,
`max_misses` and the sweep policy defaults.

## Class D — gaps found by asking Class A's question of existing code

### D1. Widening a command value into a wider typed field

`src/core/model/appliance.c:230` writes a write-command value into a multi-byte
item by zero-filling the high bytes:

```c
/* One item, big endian, high bytes zero: the command carried a
 * two-byte value and widening it says nothing untrue. */
```

That is layout B from A2, one layer up and applied to a wider field. The comment
holds for unsigned types. For a signed wider type — `INT16`, `INT32`, `INT64` —
zero-filling and sign-extending differ for exactly the negative values, which is
A2's question again.

It is not currently a defect, and this inventory should say why rather than
inflate it: the command value is `uint16_t` from the northbound parser
(`src/core/mqtt/control.c:89`) through `appl_write_item_t.value`, so there is no
negative value to widen. The gap is that this is nowhere stated and nowhere
tested:

- the only multi-byte write test uses `0x22` (`UINT32`) with a positive value
  (`tests/test_appliance.c:242`), so the signed case is never exercised;
- a deployment wanting to command a negative value for a signed service has no
  way to express one, and will discover this rather than read it.

**Recommended**: a test that pins the current behaviour for a signed wider type,
and a sentence in `appliance.h` saying the command path is unsigned by contract.
Both are cheap and neither changes behaviour.

## Summary

### R21 follow-up (2026-09-05)

The R21 manuscript adopts the two-byte INT16 interpretation explicitly (B2).
This lab retains that premise and tests its sensitivity; it does not claim to
resolve the standard. The request-side A/B rules are extended to service
responses as a modeling assumption, since the manuscript itself describes the
response wording as less explicit (A2). Neither a solver nor enumeration can
confirm that interpretive step.

The missing metadata port, config loader and three response examples are now
available as a **lab-only research subject**, not production integration.
See [paper-r21.md](paper-r21.md) for the complete mapping and assumptions.
Its arbitrary-adapter proof quantifies over return values, not memory-unsafe
callback implementations. Its registration adapter consumes already decoded
typed records and never infers types from general-format support bytes.

Evidence gates now reject SMT unknown, incomplete/duplicate grids, stale source
fingerprints, and CBMC tool errors in place of expected counterexamples.

| | Entry | Documented | Sweepable | Swept |
|---|---|---|---|---|
| A1 | registration continuation | yes, three places | yes | no |
| A2 | general-format data field | yes | yes | **yes** |
| A3 | differing write echo | yes | no | — |
| B1 | `INT64` lengths | yes | n/a | — |
| B2 | `INT16` length | **no** | yes | **yes** |
| D1 | widening a command value | **no** | yes | no |

Two entries are undocumented, and the sensitivity run says one of them (B2) is
the single width judgement in the table that changes the matrix.
