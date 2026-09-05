# Reproducing R21

Start at [paper-r21.md](paper-r21.md) for the comparison with the supplied
manuscript. Its historical measurements are not used as acceptance thresholds.

## Environment

Python 3.9+ with venv, and `lab/requirements.txt`. In the repository root:

```powershell
.\scripts\install.ps1
```

On POSIX use `sh scripts/install`. Both list discovered local interpreters and
ask for a number. For an explicit selection or CI:

```sh
python scripts/install.py --python /path/to/python3
```

`--list` does not change anything. `--dry-run` prints commands.
`--reuse` only permits an existing .venv based on the same interpreter.
The installer does not delete environments or change global Python packages.
This uses the standard [venv mechanism](https://docs.python.org/3/library/venv.html).

## Python-only run on the development machine

```powershell
.\.venv\Scripts\python.exe -m lab.run --output out/r21
```

It runs Python regression tests, enumeration, SMT, bidirectional comparison,
width sensitivity and paper assertions. C and CBMC are explicitly skipped.
An SMT unknown, missing dependency, incomplete grid, stale fingerprint or
disagreement gives a nonzero exit code. A full run cannot be claimed from this
partial result.

## Full C and proof run in CI

The `verify` job in `.github/workflows/ci.yml` builds on Ubuntu 24.04 with
GCC, AddressSanitizer, UBSan and warnings as errors, and installs CBMC 6.11.0
from its official release. It runs:

```sh
mkdir -p out
gcc -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic \
  -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -Isrc -Ilab/subject -Ilab/proofs lab/subject/experiment.c \
  lab/subject/metadata.c lab/proofs/reference.c \
  src/core/taiseia/general.c src/core/taiseia/datatype.c \
  src/core/taiseia/frame.c -o out/lab-experiment
.venv/bin/python -m lab.run --output out/r21 \
  --c-experiment out/lab-experiment --cbmc cbmc --require-full
```

The compiled experiment evaluates every value in all 80 cells and every byte
pair in all 40 exactness rows. It independently compares C output to the C
reference, then Python compares measured counts against its own enumeration.
The three adapters process all three raw response patterns. A corrupted XOR
checksum is checked separately from invalid padding.

The runner executes the three production-codec harnesses and eleven metadata
entry points, then validates both deliberate signed-conversion counterexamples.
Every proof retains unwinding assertions and conversion/memory/overflow checks.
No compiler or CBMC is installed or run on the primary development machine.

The existing product build, unit, serial/socket, simulator and broker CI jobs
remain enabled. Separate Python tests cover Windows/Linux and Python 3.9/3.14.

## Output and provenance

| File | Meaning |
|---|---|
| `manifest.json` | input commit, dirty state, LF-normalized source SHA-256, platform, commands, exit codes, timestamps and stage statuses |
| `matrix.json`, `smt.json` | independently generated matrix and exactness evidence |
| `sensitivity.json` | consequences of alternative width premises |
| `c-experiment.json` | measured C matrix, exactness, response cases and check totals |
| `paper.json` | R21 claim comparison and explicit limitations |
| `proofs/all.json` | CBMC version, commands, elapsed times, properties and failures |
| `proofs/h_*.json` | full CBMC JSON output, including both demonstration traces |
| `*.log` | captured stdout/stderr for reproduction and diagnosis |

CI uploads `r21-evidence`, even on failure. Evidence intended for citation is
copied into `lab/results/r21/` after review, preserving the original commit and
CI run URL. A separate evidence commit does not change what source was tested.
Never describe a partial/failed manifest as a full successful run.

Use a new output directory for each experiment. `--force` explicitly allows
replacement of known output files; the runner never deletes a directory tree.
Individual historical oracle commands are still available, but old files
without current source fingerprints cannot pass the new agreement gate.

## Measurements

Use the committed run records for current property counts, check totals and
times. The original R21 counts (including its 5.95.1 CBMC run), line counts and
timings were produced elsewhere and are not established by the starting
checkout. Changing tool versions, bounds or assertions changes these values.
Elapsed times describe one run, not a performance benchmark.
