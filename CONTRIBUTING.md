# Contributing

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

Sanitizers are on by default off Windows and do not recover. Do not turn them
off to get a green run.

On Windows the generator is multi-configuration: build with `--config Debug`
and test with `ctest --test-dir build -C Debug`.

CI additionally builds with warnings as errors, in release without sanitizers,
and on MSVC; runs the broker integration test; runs the lab's oracle, compiled
experiment and proofs (`verify`); and runs the lab's Python tests on Linux and
Windows. Every job must pass. [docs/testing.md](docs/testing.md) lists what
each one adds.

Changes under `lab/` have their own setup — see [lab/README.md](lab/README.md).

## The three rules that are not negotiable

**1. `src/core` and `src/ports` are freestanding.** No platform headers, no
allocation, no I/O. Checked by the `core_is_freestanding` test. Needing
something from the platform means adding a port — see
[docs/porting.md](docs/porting.md).

**2. No licensed standard material enters the repository.** Cite CNS 16014 and
CNS 16090 by clause number freely; reproduce their text, tables or figures
never. The per-appliance service tables in Annex A are specifically out, in any
form — header, data file, or comment. The standards are sold per seat and
watermarked per download; the PDFs must not be committed and `.gitignore`
blocks them.

**3. Say what you verified.** No physical appliance has ever been connected to
this code. A pull request that implies otherwise is worse than one that admits
a gap.

## Style

C99, declarations at the start of a block, `/* */` comments, four spaces.

- Every fallible function returns `result_t`. Nothing that can fail returns
  `void`.
- Buffer sizes are derived in `src/core/taiseia/limits.h` and referenced by
  name. Never hand-compute a size next to the buffer it bounds.
- Every parse entry point takes the caller's capacity as a parameter. Validate
  wire lengths against both the bytes actually held and the destination's
  capacity, before writing anything.
- Prefer refusing malformed input to truncating it.

Comments explain why, not what. A comment restating the line below it will be
asked about in review.

## Tests

Ship the test with the behaviour, in the same commit. Name the test function
after the property it establishes — `reserved_lengths_are_refused`, not
`test_declared_len`.

Test the refusals. Most of what a protocol decoder owes its caller is the
malformed input it declines.

## Commits and pull requests

Conventional commits: `feat(core):`, `fix(sim):`, `docs:`, `test:`, `chore:`.

Work on `develop`; `main` takes merges through pull requests.

A pull request says what changed, what was verified, and what was not. If it
changes a decision recorded in [docs/adr](docs/adr), update the ADR in the same
pull request — supersede it rather than editing history into it.

## Decisions

Before proposing an alternative approach, check [docs/adr](docs/adr). Each one
ends with the alternatives that were considered and why they were not taken. If
your reasoning is not answered there, that is a good sign the ADR is incomplete
and worth saying so.
