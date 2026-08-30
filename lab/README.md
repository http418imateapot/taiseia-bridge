# lab — checking the expected values

The test suite in `tests/` checks that this program does what we expect. This
directory checks whether what we expect is right.

That is a different question and the tests cannot answer it. An assertion
compares an output against an expected value; the expected value came from a
person reading a document; and where that document permits more than one
reading, the assertion silently encodes whichever one the person picked. The
suite then passes, on every platform, forever, whichever reading was picked.

`lab` is not part of the program. Its Python builds nothing and ships nothing,
and its C — under `lab/proofs/` — is never linked into the program either. The
layering rule in [AGENTS.md](../AGENTS.md) governs `src/core` and `src/ports`;
it has nothing to say about this directory.

## Run it

```bash
pip install z3-solver
```

```bash
python -m lab.oracle
```

That is steps 1 to 5 below. Non-zero exit means two independent transcriptions
of the oracle disagree, so at least one is wrong and nothing downstream is worth
running until it is settled.

Step 6 needs `cbmc` and a C compiler, which the machine this was written on does
not have. It runs in CI:

```bash
make -C lab/proofs verify
```

## The six steps

Each step has one job, one file, and one thing that goes wrong if you skip it.

### 1. Name the decision and make it a parameter

`oracle/layouts.py`, `oracle/readings.py`

Find where the code or a test picks one reading of prose that permits several,
and enumerate the axis instead of hard-coding the pick. Here: `D1` is what a
sender does with a one-byte value in a two-byte field (two layouts); `D2` is what
a receiver does with the bytes above the value (four readings).

*Skip it and* the choice stays invisible, and the test suite encodes it without
anybody deciding.

### 2. Work out what actually needs checking

`oracle/types.py` — `python -m lab.oracle.types`

Derives the ten types the field can carry by **reading**
`src/core/taiseia/datatype.c`, not by transcribing it again. Eight are two bytes
wide, so the value fills the field and the two layouts coincide; `UINT8` is one
byte and unsigned, so the two rules agree; `INT8` is one byte and signed, and is
where they collide.

*Skip it and* you check 80 cells without knowing which 8 could possibly differ,
and you cannot tell a real result from an arithmetic accident.

### 3. Write the oracle as arithmetic

`oracle/sweep.py` — equations (1)–(4)

`F(t, s, r)` is the set of values of type `t` that do not survive being written
by layout `s` and read by reading `r`. A cell is safe when it is empty.

*Skip it and* the expected value lives in the test that uses it, where nothing
can check it.

### 4. Check the oracle before checking anything with it

`oracle/smt.py`, `oracle/agree.py`

Three routes to the same table that must agree cell by cell: hand proof (the
propositions at the end of [method.md](docs/method.md)), exhaustive enumeration, and a
second transcription for z3. `agree.py` compares the last two and exits non-zero
on any disagreement.

The solver adds **no strength** at this scale — the largest domain has 65,536
members and enumeration tries all of them. It adds *independence*, which is the
only defence against an oracle that is confidently and consistently wrong.

*Skip it and* you have one transcription of ambiguous prose and no way to know it
is wrong.

### 5. Ask whether the oracle is vacuous

`oracle/sweep.py`, the exactness pass

Both directions: does the reading accept a byte pair no layout emits (`over`),
and does it reject one some layout does emit (`under`)? Only a reading with
neither is exact.

*Skip it and* — this is not hypothetical — the first version here measured
over-acceptance only and reported three of the four readings as exact when one
is. Worked example in [method.md](docs/method.md) step 5.

### 6. Apply it to the implementation

`tests/test_general.c`, then `lab/proofs/` — see [proofs.md](docs/proofs.md)

The exhaustive unit test walks the same 80 cells in C; its counts must equal
`results/matrix.json`. Then CBMC compares the implementation against a third
transcription of the equations for *arbitrary* inputs, with conversion and
overflow checks on — because the test runs one compilation, and C99 leaves
signed conversion implementation-defined exactly where sign extension needs it.

*Skip it and* you have proved things about Python and shipped C.

## Turning the method on the project itself

Steps 1–6 produce a table about the standard. The project-facing question is one
step further: **which of the premises the table rests on are decisions somebody
made, and would a different decision change the table?**

`oracle/sensitivity.py` asks that of the type widths. Two of the ten are
sensitive, and `INT16` is the one where the published table is disputed and the
implementation's judgement is not written down.

That is the shape worth repeating on every entry in
[oracle-audit.md](docs/oracle-audit.md) — not "here is the right answer", but
"here is which of our decisions the answer depends on".

## What is here

| | |
|---|---|
| [docs/oracle-audit.md](docs/oracle-audit.md) | **start here.** Every place this project's expected values are a decision rather than a fact |
| [docs/method.md](docs/method.md) | steps 1–5 in full, and the propositions in longhand |
| [docs/proofs.md](docs/proofs.md) | step 6: the harnesses, the flags, and the two that matter |
| [docs/reproduce.md](docs/reproduce.md) | versions, commands, and the runs the documents quote |
| `oracle/` | the instrument |
| `proofs/` | the CBMC harnesses, the reference transcription, and the naive version kept so the proof can be shown catching it |
| `results/` | committed output, because a result nobody can point at is not evidence |

## What it has established

**On the standard.** Two conforming layouts, four plausible readings, 80
combinations, 2 of which misread — both `INT8`, 128 values each, witnessed by
−128. No layout is safe against both strict readings, so a sender cannot close
this alone. A `lenient` receiver can, and is the only one of the four readings
that accepts a byte pair if and only if some conforming sender emits it.

**On this project.** Two premises of the test suite are undocumented, and one of
them is load-bearing: `INT16`'s value length is the single width judgement in
`src/core/taiseia/datatype.c` whose alternative moves a misreading cell. The row
two below it carries a `NOTE` for a disagreement of exactly the same kind.

**On this directory.** The first exactness check here was one-directional and
reported three of four readings as exact when one is. It was caught by asking
the second question, not by running anything.

## What it does not establish

Nothing here is evidence about a real appliance. These are claims about
equations, about a type table, and about C source — checked two and three ways.
Whether any shipping firmware picks layout A or layout B is not knowable from
here, and the point of the matrix is that the wire does not say. See
[ADR 0010](../docs/adr/0010-verification-without-hardware.md).

The codec proved in step 6 is not wired to anything: nothing in the program calls
it. Publishing typed values is
[ADR 0014](../docs/adr/0014-typed-general-data-field.md), and it is still
proposed.
