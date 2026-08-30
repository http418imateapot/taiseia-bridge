# ADR 0013 — Expected values are checked before they are used, in a lab that ships nothing

Status: accepted

## Context

Every assertion in `tests/` compares an output against an expected value that a
person derived from a document. The suite can check the output. It cannot check
the expected value, because the expected value is what it checks against.

For most of this project that is a small risk, because most expected values are
determined: a checksum is a checksum. It stops being small wherever the standard
permits more than one reading, and this protocol has several such places —
`src/core/taiseia/reassembly.h` names one and marks it as an inference,
[ADR 0011](0011-write-outcome-vocabulary.md) names another, `datatype.c` carries
a `NOTE` about a third. In those places two implementations can both follow the
text and disagree on the wire, and a test written from one reading passes forever
without anyone learning that the other exists.

The failure mode is not theoretical here. `c20ba30` fixed tests that asserted a
fragment length of `len - 1`; `d87aef4` fixed a dozen tests that passed on Linux
because the stack happened to be zero and failed on MSVC because it was `0xCC`;
`b5b54b1` fixed two checks that read back a service they had not written. All
three passed, or would have passed, without establishing the property. Two were
caught by a second compiler rather than by the assertions.

There is also a constraint that shapes the answer: there is no compiler on the
primary development machine. Anything that must run there cannot be C.

## Decision

A `lab/` directory at the repository root whose subject is the expected values
rather than the program.

**It ships nothing.** No CMake target, no build step, no artefact. It is Python.
Nothing under `src/` may depend on it, and the freestanding rule that governs
`src/core` and `src/ports` does not reach it — the "no external libraries"
constraint in this project is about run time, and `lab/` has no run time.

**An oracle is checked before it is used**, by three routes that must agree: hand
proof, exhaustive enumeration, and a second transcription of the same equations
for an SMT solver. `lab/oracle/agree.py` compares the last two cell by cell and
exits non-zero on any disagreement.

**Both directions of vacuity are checked.** A reading that accepts byte pairs no
conforming sender emits satisfies "never misreads a conforming value" for the
wrong reason. A reading that rejects pairs a sender does emit refuses valid
traffic. An oracle is exact only with neither, and checking one direction alone
reported three of four readings here as exact when one is.

**The premises are swept too.** Where a table entry is a judgement rather than a
transcription, `lab/oracle/sensitivity.py` asks whether the answer depends on it.

**Results are committed** to `lab/results/`, and no document may cite a count, a
verdict or a timing without one of them behind it.

**The audit is the deliverable**, not the matrix.
[lab/docs/oracle-audit.md](../../lab/docs/oracle-audit.md) inventories where this
project's expected values are decisions, and it is the file to update when a new
one is found or an old one is closed.

## Consequences

- The decisions the test suite rests on are in one list instead of scattered
  across comments, and can be reviewed as a set.
- Work on the expected values proceeds on a machine with no compiler, which is
  the machine this project is written on.
- Applying an oracle to the C is a separate, later, more expensive step, and
  [docs/verification.md](../verification.md) says plainly that it has not been
  taken. The lab establishes what the right answer is; it does not yet establish
  that the C computes it.
- A second language in the repository. Contributors need Python for `lab/` and
  nothing for the program, which is an acceptable split precisely because the two
  do not touch.
- Committed results go stale if the equations change and nobody re-runs them.
  `python -m lab.oracle` exits non-zero on disagreement, so this is a thing CI can
  hold, and does not.
- An `unsat` is a claim about the equations handed to the solver and nothing more.
  The lab cannot say which layout a real appliance picked. Nothing here weakens
  [ADR 0010](0010-verification-without-hardware.md); the lab is a third substitute
  alongside CI and the simulator, with its own boundary.

## Alternatives rejected

**Put the checks in `tests/`.** They would need a compiler, so they could not run
where the work is done; and they would be checking the expected values using the
same harness whose expected values are in question, which is the confusion this
ADR exists to avoid.

**Keep it in a separate repository.** The audit is about *this* code and cites
line numbers in it. Split across repositories the two drift, and the audit is
worth less than the drift costs.

**Write the type table into the lab.** A second transcription of a table that
already exists in `src/core/taiseia/datatype.c` can disagree with it, and would
fail in the direction of agreeing with itself. `lab/oracle/types.py` reads the C
instead.

**Skip the solver, since enumeration is already complete at this scale.** True,
and the solver is not there for strength — it is there because two independent
transcriptions catch a mistake that one cannot, and because the method stops
working at the first domain too large to walk.
