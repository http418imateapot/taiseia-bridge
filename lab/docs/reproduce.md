# Reproducing the runs

Everything in this directory is Python and runs on a machine with no compiler,
which is the point: it checks expected values, and expected values are not
compiled.

## What you need

Python 3, and z3 for the second transcription:

```bash
pip install z3-solver
```

z3 is optional in the sense that `sweep.py` and `sensitivity.py` run without it.
It is not optional in the sense that matters — without it the enumeration is
checked against nothing, and `python -m lab.oracle` exits non-zero and says so.

## Running it

From the repository root:

```bash
python -m lab.oracle
```

That runs all four stages in order and exits non-zero if the two transcriptions
disagree on any cell. The stages can also be run alone:

```bash
python -m lab.oracle.sweep
```

```bash
python -m lab.oracle.smt
```

```bash
python -m lab.oracle.agree
```

```bash
python -m lab.oracle.sensitivity
```

`types.py` has a `__main__` too, which prints the carryable set it derived from
`src/core/taiseia/datatype.c` — useful when a row in that table changes and the
matrix moves.

## Step 6 needs more than Python

The proofs need `cbmc` and a C compiler. Neither is on the machine this was
written on, so **step 6 has never been run here** and cannot be. It runs in the
`verify` CI job:

```bash
make -C lab/proofs verify
```

```bash
make -C lab/proofs demo
```

`verify` must pass; `demo` must **fail**, because it demonstrates CBMC rejecting
the obvious way of writing sign extension. Logs go to `lab/results/proofs/`,
which is empty until CI has produced them. The CBMC version that may be quoted
is the one in `lab/results/proofs/versions.txt` — CI installs whatever the
distribution ships, and that is deliberately not pinned to a number written down
in advance.

See [proofs.md](proofs.md).

## Output

Written to `lab/results/`, and committed, because a result nobody can point at is
not evidence:

| File | What it holds |
|---|---|
| `matrix.json` | the 80 cells by enumeration, plus exactness in both directions |
| `smt.json` | the same 80 cells by z3, with `unsat` recorded per cell |
| `sensitivity.json` | how the matrix moves if a type width had been judged the other way |
| `run.log` | console output of the last full run |

## The run these documents quote

| | |
|---|---|
| Date | 2026-08-30 |
| Machine | Windows 10, single run, not a benchmark |
| Python | 3.9.13 |
| z3 | 5.1.0 |
| enumeration | 13.20 s |
| solver | 1.48 s |
| sensitivity | 18.40 s |
| agreement | 80 cells and 40 exactness checks, no disagreement |

These are the figures in `lab/results/*.json` as committed; re-running moves them
by a second or so. They are wall clock on one desktop and are here so a reader
knows the order of magnitude — seconds, not hours — not as a measurement of
anything. The solver is
faster than the enumeration here because the enumeration walks 256 × 256 byte
pairs per type per reading for the exactness check, and the solver asks for one
witness.

## What these numbers are not

They are not the numbers in any earlier write-up of this work, and they should
not be reconciled with one. They are what this code produced on this machine on
this date. If an external document quotes different figures, the document is the
thing to correct.

Nor do they say anything about a real appliance. Every result here is about the
equations in `lab/oracle/` and the type table in `src/core/taiseia/datatype.c`.
No appliance has ever been connected to this project; see
[docs/testing.md](../../docs/testing.md) and
[ADR 0010](../../docs/adr/0010-verification-without-hardware.md).
