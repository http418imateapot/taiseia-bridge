# Paper verification lab

This directory reproduces the POWER2026 R21 paper's equations and experiments.
It checks the production general-field codec and a **lab-only** metadata
prototype. It ships no product code and adds no product CMake target.

[正體中文對照與限制](docs/paper-r21.md) · [Reproduce](docs/reproduce.md) ·
[Oracle audit](docs/oracle-audit.md) · [Proofs](docs/proofs.md)

## Quick start

Windows:

```powershell
.\scripts\install.ps1
.\.venv\Scripts\python.exe -m lab.run --output out/r21
```

POSIX:

```sh
sh scripts/install
.venv/bin/python -m lab.run --output out/r21
```

The installer discovers local base Python interpreters (3.9+) and asks you to
select one. It creates a private project venv and installs pinned dependencies.
`--python PATH` selects explicitly for unattended runs; `--list`, `--dry-run`
and `--help` are available. Existing environments require `--reuse` and must
match the selected interpreter. No global packages or Git identities are changed.

The default run does the Python work and labels C/proofs **skipped**, with overall
status **partial**. A passing Python exit code is not a claim that C passed.
CI supplies the compiled experiment and CBMC and uses `--require-full`.

## R21's six steps

| Step | Executable or document | Result |
|---|---|---|
| 1 Interface and external type premise | `subject/metadata.c` | config, typed registration and absent-source adapters |
| 2 Type classification | `oracle/types.py`, `paper.py` | Table 2, derived from the existing C table |
| 3 Arithmetic oracle | `oracle/layouts.py`, `oracle/readings.py` | equations (1)–(4); hand route in `docs/method.md` |
| 4 Check the oracle | `oracle/sweep.py`, `smt.py`, `agree.py` | 80 cells and 40 bidirectional exactness checks |
| 5 Check C against the oracle | `subject/experiment.c`, `proofs/` | compiled exhaustive comparison and bounded proofs |
| 6 Compare test-design scope | `docs/paper-r21.md` | frame corruption vs layout ambiguity; no certification claim |

`python -m lab.oracle` remains supported and writes the traditional files to
`lab/results/`. Prefer the run command for a fresh directory, provenance and a
clear distinction between partial and full evidence. Existing result directories
are protected unless `--force` is supplied.

## Boundaries

- The metadata prototype is new research code; it is not connected to
  `src/apps`, MQTT, CLI or the appliance model.
- Synthetic keys are used. No per-appliance service table, licensed document,
  real device identifier or broker configuration is included.
- The simulator experiment constructs real response packets and uses the
  production frame parser. Its peer does not call the typed decoder to choose
  what bytes to send.
- The C type table is a shared premise, not an independently proved fact.
  Sensitivity experiments retain the alternative INT16 width.
- CBMC establishes properties in its selected target model and object bounds.
  A signed conversion finding is not automatically undefined behavior in C99.
- No result here establishes what a physical appliance does. Historical paper
  timings, property totals, assertion counts and line counts are not targets to
  hard-code into tests.

The current evidence and commands are described in [reproduce.md](docs/reproduce.md).
