# Verification

[testing.md](testing.md) answers "did this run pass?". This answers a different
question: "is what we expected right, and would every input agree with it?"

The two are not degrees of the same thing. A test suite compares outputs against
expected values it cannot check, on the inputs somebody thought of. Both halves
of that are worth attacking separately, and they need different tools.

## Three strengths of claim

| Claim | Established by | Where |
|---|---|---|
| this program, this machine, these inputs, this run | the unit tests, the simulator, the broker and pty tests | `tests/`, CI |
| every value in a domain agrees with an oracle that has itself been checked | enumeration, cross-checked by a solver | `lab/` |
| every input agrees, including ones nobody enumerated | bounded model checking | not built — see below |

Rows two and three are worth having only if the oracle in row two is right, which
is why `lab/` spends most of its effort on the oracle rather than on the program.

## What `lab/` is

`lab/` checks expected values. It is Python, it compiles nothing, it ships
nothing, and no part of `src/` depends on it. Start at
[lab/docs/oracle-audit.md](../lab/docs/oracle-audit.md), which inventories every
place this project's expected values are a decision somebody made rather than a
fact the standard settles.

```bash
python -m lab.oracle
```

The short version of what it has found:

- The general format's two-byte data field admits two conforming layouts for a
  negative one-byte value and four plausible readings. Of 80 combinations, 2
  misread — both `INT8`, 128 values each. A sender cannot close this alone; a
  `lenient` receiver can, and is the only reading that accepts a byte pair if and
  only if a conforming sender emits it.
- `INT16`'s value length in `src/core/taiseia/datatype.c` is the one width
  judgement in that table whose alternative would move a misreading cell, and it
  is the only load-bearing one without a `NOTE`. `INT64`, two rows down, has one
  for a disagreement of the same kind.
- The command path widens a write value by zero-filling high bytes, which is one
  of the two layouts applied a layer up. It is not a defect — the value is
  `uint16_t` end to end, so there is no negative to widen — but neither the
  contract nor the case is written down or tested.

## The implementation, against the oracle

`src/core/taiseia/general.c` is a pure codec taking the layout and the reading as
parameters rather than as constants, with all four readings present. Nothing in
the program calls it — publishing typed values is
[ADR 0014](adr/0014-typed-general-data-field.md) and is still proposed — so it
changes no behaviour and the existing tests are its regression guard.

It is checked twice, and the two checks establish different things:

- `tests/test_general.c` walks the same 80 cells exhaustively in C. Its counts
  must equal `lab/results/matrix.json`.
- `lab/proofs/` compares it against a third transcription of the equations for
  *arbitrary* inputs under CBMC, with conversion and overflow checks on. Not more
  coverage — the test already covers every input — but a different claim, because
  the test runs one compilation and C99 leaves signed conversion
  implementation-defined precisely where sign extension needs it.

`lab/proofs/naive.c` keeps the obvious way of writing those conversions, so that
gap is demonstrable rather than argued: it passes every test in this repository,
and `make -C lab/proofs demo` expects CBMC to reject it.

**None of it has been run on the machine it was written on** — no compiler, no
CBMC. It runs in the `verify` CI job, and `lab/results/proofs/` is empty until
that job has run. See [ADR 0010](adr/0010-verification-without-hardware.md) and
[lab/docs/proofs.md](../lab/docs/proofs.md).

## What has still not been done

- **The loader and the port from ADR 0014.** The codec takes a type code; nothing
  yet supplies one from an operator's map, so the two harnesses that would prove
  a loader safe on arbitrary input do not exist.
- **The write path.** `appliance.c` widens a command value by zero-filling, which
  is the same layout question one layer up. See D1 in the
  [audit](../lab/docs/oracle-audit.md).
- **Anything about a real appliance.** Unchanged, and unchangeable from here.

## Honesty

The rule in [AGENTS.md](../AGENTS.md) — never claim something passes without a
run to point at — applies here with one addition, because proofs fail
differently from tests. **Never cite a property count, a solver verdict or a
timing without a run in `lab/results/` that produced it.** An `unsat` is a claim
about the equations that were handed to the solver, and it is exactly as good as
those equations. It is not a claim about a real appliance, and no result in
`lab/` ever will be. See
[ADR 0010](adr/0010-verification-without-hardware.md) and
[ADR 0013](adr/0013-oracle-before-code.md).
