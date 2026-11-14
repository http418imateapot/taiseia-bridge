# Proof logs

This is the default output directory of the proof runner
(`python -m lab.proofs.run`, and the `make -C lab/proofs verify` / `demo`
targets that wrap it). It is empty in the repository: the machine this project
is written on has no compiler and no CBMC, so nothing here can be produced
locally — see [ADR 0010](../../../docs/adr/0010-verification-without-hardware.md).

**The reviewed evidence is not here.** It is in [../r21/](../r21/README.md),
copied from a named CI run, with the raw CBMC output stored losslessly
compressed. Cite from there.

## What a run writes

| File | What it holds |
|---|---|
| `h_<harness>.json` | CBMC's full JSON output for one harness, including its properties and their status |
| `h_<harness>.stderr.log` | CBMC's stderr for that harness |
| `verify.json`, `demo.json` or `all.json` | the suite summary: CBMC version, each command, exit codes, property counts, failures, elapsed times |

The `verify` suite requires every property to pass; the `demo` suite requires
`h_naive` to fail with exactly the two expected signed-conversion
counterexamples. See [lab/docs/proofs.md](../../docs/proofs.md).

Property counts and timings come from the structured JSON, not from grepping a
text log. A count quoted from memory, from an earlier write-up, or from another
machine is not evidence about this code. See [AGENTS.md](../../../AGENTS.md).
