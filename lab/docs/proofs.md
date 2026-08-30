# Step 6 — applying the checked oracle to the C

Steps 1 to 5 ([method.md](method.md)) produce an oracle that has been checked
two independent ways and shown not to be vacuous. This step is what it was for:
comparing the implementation against it.

> **Nothing in `lab/proofs/` has been run on the machine this was written on.**
> There is no compiler and no CBMC here. Every number this directory produces
> comes from CI, and `lab/results/proofs/` is empty until it has run. See
> [ADR 0010](../../docs/adr/0010-verification-without-hardware.md).

## Why not just run the exhaustive test

`tests/test_general.c` already walks all 80 cells and every value of every
domain. That is complete coverage of the input space, so what is left?

What is left is that it runs a *compiled binary*. C99 does not require signed
integers to be two's complement (6.2.6.2), and converting to a signed type a
value that type cannot represent is implementation-defined (6.3.1.3) — which is
precisely what sign extension does. gcc and MSVC both do the wanted thing, so
the test passes on both, and neither run says anything about the source.

That gap is not closable by testing harder. It needs a tool that reasons about
the source rather than executing one compilation of it.

`lab/proofs/naive.c` and `make demo` exist to make this concrete rather than
theoretical: the obvious way to write those two conversions passes every test in
this repository and CBMC rejects it.

## The three proofs

Each harness leaves its inputs unconstrained and asserts the implementation
agrees with `reference.c`, which is equations (2)–(4) transcribed a third time.

| Harness | Inputs left arbitrary | What it establishes |
|---|---|---|
| `h_encode.c` | type code, layout, value over all of `int32_t` | encode accepts exactly the values the equations accept, and writes the bytes they give |
| `h_decode.c` | type code, reading, both bytes | decode accepts exactly the pairs the equations accept, and returns the values they give |
| `h_roundtrip.c` | type code, layout, value in domain | P2 and P1 from [method.md](method.md), for any type rather than for `INT8` |

The type code is deliberately unconstrained across all 256 values, so the 246
codes the general format cannot carry are covered too — refusal is proved, not
assumed.

## The flags, and the two that matter

```
--bounds-check --pointer-check --pointer-overflow-check
--conversion-check --signed-overflow-check --unsigned-overflow-check
--div-by-zero-check --undefined-shift-check
--unwind 32 --unwinding-assertions
```

**`--conversion-check`** is the one that earns this step its place. It is what
separates "the compiler did what we wanted" from "the source says what we
meant", and it is what `make demo` demonstrates.

**`--unwinding-assertions`** is not optional. `ts_datatype()` scans a 25-row
table, so there is one loop and `--unwind 32` bounds it. Without the assertion
an insufficient bound silently proves a *smaller program than the one you have*
— a green result about a truncated loop. That is the same vacuity failure as a
reading that accepts everything, wearing different clothes, and this directory
has already been caught by one of those. If the type table ever grows past the
bound, the proof fails loudly instead.

## Assume versus assert

The rule used throughout: **assume what a conforming sender is entitled to do;
assert what the code is obliged to do.**

`h_roundtrip.c` asserts that an in-domain value always encodes successfully
rather than assuming it. Assuming would prune exactly the paths where the
encoder wrongly refuses a valid value, and the round-trip property below it
would then hold for no reason. An assumption that quietly deletes the
counterexample is the single easiest way to get a green proof of nothing.

## Running it

Needs `cbmc` on the path. In CI that is `apt-get install -y cbmc`; the version
it reports is the version that may be quoted, and no other.

```bash
make -C lab/proofs verify
```

```bash
make -C lab/proofs demo
```

`verify` must pass. **`demo` must fail** — the Makefile treats a clean
verification there as the error, because the demonstration is that CBMC rejects
what the tests accept. Logs land in `lab/results/proofs/`.

For the paper's numbers, the per-harness property counts and solver times are in
those logs:

```bash
grep -E "^\*\* [0-9]+ of [0-9]+ failed|Runtime decision procedure" lab/results/proofs/*.log
```

## What this does not establish

**The type table is a shared premise, not a proved one.** Both the
implementation and the reference ask `ts_datatype()` for a type's width and
signedness. Keeping a second copy in the reference would be the drift this lab
exists to avoid, so the proofs establish that the codec agrees with the
equations *given the table* and say nothing about whether the table is right.
That question is [oracle-audit.md](oracle-audit.md) B1 and B2, and it is
answered — as far as it can be — by `lab/oracle/sensitivity.py`.

**Nothing here is evidence about a real appliance.** A proof is a claim about
the source and the equations handed to the checker. Which layout any shipping
firmware actually picked is not knowable from here, and the whole point of the
matrix is that the wire does not say.

**The write path is not covered.** `appliance.c` widens a command value into a
multi-byte item by zero-filling, which is the same question one layer up; see
D1 in [oracle-audit.md](oracle-audit.md). The codec proved here is not yet wired
to anything — that is [ADR 0014](../../docs/adr/0014-typed-general-data-field.md),
and it is still proposed.
