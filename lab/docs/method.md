# Checking an expected value before trusting it

A test suite is a set of claims of the form "this input should produce that
output". The suite can check the first half. It cannot check the second, because
the expected value is the thing it is checking against. Where the expected value
came out of a document that a person read, the whole suite rests on that reading,
and a suite whose oracle is wrong is not weakly right — it is confidently wrong,
and green.

That is a general problem. It gets sharp in a protocol with no self-description,
because then the wire carries nothing to check the reading against: no type tag,
no length discriminator, nothing that would make a misreading show up as a
malformed packet rather than as a plausible number. See
[oracle-audit.md](oracle-audit.md) for where this project's expected values come
from and which of them are decisions rather than facts.

The method here is six steps. It is not novel and does not need to be; what it
needs is to be cheap enough to run on a real test suite.

## 1. Find the decision and make it a parameter

Look for a place where the code, or a test, picks one reading of prose that
permits several — and instead of hard-coding the pick, name the axis and
enumerate its values.

For the two-byte data field there are two axes. `D1` is what a sender does with a
one-byte value in a two-byte field: `lab/oracle/layouts.py`. `D2` is what a
receiver does with the bytes above the value: `lab/oracle/readings.py`. The
implementation still has to choose, but the choice becomes a value the analysis
ranges over rather than a constant nobody can see.

## 2. Work out what actually needs checking

Not every combination is interesting, and the analysis is cheaper and more
readable if it says which ones are not, and why, before running anything.

`lab/oracle/types.py` derives the set of types a general-format field can carry —
those one or two bytes wide — by reading `src/core/taiseia/datatype.c` rather
than transcribing it a second time. That yields ten. Eight are two bytes wide,
so the value fills the field and both layouts coincide; one is one byte and
unsigned, so the two rules that conflict agree; one, `INT8`, is one byte and
signed, and is where they can disagree.

Deriving the set from the implementation rather than re-typing it is deliberate.
A second transcription can drift from the first, and a drifted oracle fails in
the direction of agreeing with itself.

## 3. Write the oracle as arithmetic, separate from the code

The expected value should be computable from the specification without
consulting the implementation. Equations (1) to (4) in `layouts.py`,
`readings.py` and `sweep.py` do that: `F(t, s, r)` is the set of values of type
`t` that do not survive being written by layout `s` and read by reading `r`. A
cell is safe when the set is empty.

## 4. Check the oracle before checking anything with it

Three independent routes to the same table, which must agree cell by cell:

| Route | File | What it is |
|---|---|---|
| enumeration | `lab/oracle/sweep.py` | every value of every domain, tried |
| solver | `lab/oracle/smt.py` | the same equations transcribed again, for z3 |
| hand | this document, and the propositions below | integer arithmetic, checkable by reading |

`lab/oracle/agree.py` compares the first two and exits non-zero on any
disagreement.

**The solver adds no strength at this scale, and saying otherwise would be the
most tempting dishonest sentence here.** The largest domain has 65,536 members
and enumeration tries all of them, which is a complete proof. What the solver
adds is *independence*: a second transcription of the same prose, written against
a different evaluator. When two independent transcriptions disagree, one is
wrong, and it shows up before either judges an implementation. That is the only
available defence against an oracle that is consistently wrong, and no amount of
running the implementation provides it. The solver also keeps the method usable
past the point where enumeration stops — a wider field, a scaling layer, two
fields combined.

## 5. Ask whether the oracle is vacuous

A test that always passes is indistinguishable, from inside, from a test that
passes because the property holds. The oracle version of this is a reading that
accepts too much: `low` never misreads any conforming value, and it also reads
`12 FD` as −3, which no conforming sender ever emits. It satisfies step 4 for the
wrong reason.

So each reading is asked a second question in both directions:

- **over** — does it accept a byte pair no layout emits?
- **under** — does it reject a pair some layout does emit?

Only a reading with neither is exact. Checking one direction is not enough, and
this is not a hypothetical: the first version of `sweep.py` in this directory
measured over-acceptance only and reported three of the four readings as exact.
With both directions, one is:

```
INT8      accepts   over    under
wide          256      0      128
low         65536  65152        0
strict        256      0      128
lenient       384      0        0
```

## 6. Then, and only then, apply it to the implementation

With a checked oracle, the implementation can be compared against it — twice,
because the two comparisons establish different things.

`tests/test_general.c` walks the same 80 cells and every value of every domain
in C. Its counts must equal `lab/results/matrix.json`; a disagreement means one
of the three transcriptions is wrong, and making the test pass is not how you
find out which.

`lab/proofs/` then compares the implementation against a *third* transcription
of the equations for arbitrary inputs, with conversion and overflow checks on.
That is not more coverage — the test already covers every input. It is a
different kind of claim: the test runs one compilation, and C99 leaves signed
conversion implementation-defined exactly where sign extension needs it, so a
green test on gcc and MSVC says nothing about the source. See
[proofs.md](proofs.md), and `lab/proofs/naive.c`, which passes every test in this
repository and which CBMC rejects.

## Turning the method on the project itself

Steps 1 to 5 produce a table. The table is about the standard, not about this
project. The project-facing question is one step further on: **which of the
premises the table rests on are decisions somebody made, and would a different
decision change the table?**

`lab/oracle/sensitivity.py` asks exactly that of the type widths. Each carryable
type's width is flipped to the other plausible value and the matrix is recomputed.
Two of the ten are sensitive — the two signed ones — and of those, `INT16` is the
one where the published table is disputed and the implementation's judgement is
not written down. See B2 in [oracle-audit.md](oracle-audit.md).

That is the shape worth repeating on the other entries in the audit: not "here is
the right answer", but "here is which of our decisions the answer depends on".

## The propositions, for step 4's hand route

Restricted to `INT8`, where `V = [−128, 127]`, and writing `⟦b⟧` for
`256·hi + lo`.

**P1 — two cells misread, 128 values each.** For `v` in `[−128, −1]`:
layout B gives `(00, 256+v)`, so `⟦b⟧ = 256+v` lies in `[128, 255]`, above the
upper bound, and `wide` rejects it. Layout A gives `(FF, 256+v)`, and `strict`
requires a zero high byte, so it rejects it. For `v ≥ 0` the two layouts coincide
and nothing is rejected. Hence exactly 128 values in each of those two cells, and
`−128` witnesses both.

**P2 — `lenient` never misreads.** For a two-byte type the layouts coincide and
the field is read whole. For a one-byte type the low byte is `v mod 256` under
both layouts, and the high byte is `00`, or `FF` only when `v < 0` — both of
which `lenient` admits.

**P3 — `lenient` accepts no more than that.** It accepts `b` when the high byte
is `00`, in which case `b` is layout B's image of `σ₈(lo)`; or when the high byte
is `FF` and `σ₈(lo) < 0`, in which case `b` is layout A's image of that value.
So every pair it accepts is emitted by some layout, and with P2 the two sets are
equal.

**P1 and P2 together**: no layout is safe against both `wide` and `strict`, so a
sender cannot close this by choosing well. A `lenient` receiver is the only
unilateral fix, and P3 says it buys that without accepting traffic no conforming
sender produces.
