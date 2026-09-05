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
| inputs within the declared target model and object bounds agree | bounded model checking | `lab/proofs/`; results require a cited CI run |

Rows two and three are worth having only if the oracle in row two is right, which
is why `lab/` spends most of its effort on the oracle rather than on the program.

## What `lab/` is

`lab/` checks expected values with Python and independent C experiments/proofs
compiled in CI. It ships nothing, and no part of `src/` depends on it. Start at
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

- `tests/test_general.c` walks the same 80 cells exhaustively in C.
  `lab/subject/experiment.c` additionally emits measured counts for all 80 cells
  and 40 exactness rows, which `lab/paper.py` compares against the Python run.
- `lab/proofs/` compares it against a third transcription of the equations for
  arbitrary modeled inputs under CBMC, with conversion and overflow checks on.
  This claim is bounded by CBMC's target machine model, object capacities and
  stated assumptions; it is not a proof for every C99 implementation.

`lab/proofs/naive.c` keeps the obvious way of writing those conversions, so that
gap is directly checkable: `make -C lab/proofs demo` requires CBMC to report both
specific signed-conversion counterexamples. Implementation-defined conversion
is not itself undefined behavior; tool failures do not count as counterexamples.

**None of it has been run on the machine it was written on** — no compiler, no
CBMC. It runs in the `verify` CI job, which publishes `r21-evidence`. Reviewed
results are committed under `lab/results/r21/`. See [ADR 0010](adr/0010-verification-without-hardware.md) and
[lab/docs/proofs.md](../lab/docs/proofs.md).

## What has still not been done

- **Production metadata integration from ADR 0014.** A loader, port and typed
  read/write path now exist as a lab-only research subject, with eleven proof
  entries. They are not connected to the production application. See the
  [R21 comparison](../lab/docs/paper-r21.md).
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
