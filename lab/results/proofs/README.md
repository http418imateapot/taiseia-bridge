# Proof logs

The reviewed R21 run is now stored in [../r21/](../r21/README.md), including
losslessly compressed raw CBMC output. This directory remains the output of
standalone `make verify` / `make demo` commands; the older description below
records the original, pre-R21 setup.

Empty until CI has run. The machine this project is written on has no compiler
and no CBMC, so nothing here can be produced locally — see
[ADR 0010](../../../docs/adr/0010-verification-without-hardware.md).

The `verify` job in [.github/workflows/ci.yml](../../../.github/workflows/ci.yml)
runs `make -C lab/proofs verify` and `make -C lab/proofs demo`, and uploads this
directory as a build artifact. After a run it holds:

| File | What it holds |
|---|---|
| `versions.txt` | the CBMC version that produced the rest |
| `h_encode.log` | full CBMC output, including the property count and the solver time |
| `h_decode.log` | as above |
| `h_roundtrip.log` | as above |
| `h_naive.log` | the demonstration, which is expected to REPORT FAILURES |

Property counts and timings for citation come from those logs and nowhere else:

```bash
grep -E "^\*\* [0-9]+ of [0-9]+ failed|Runtime decision procedure" lab/results/proofs/*.log
```

A count quoted from memory, from an earlier write-up, or from another machine is
not evidence about this code. See [AGENTS.md](../../../AGENTS.md).
