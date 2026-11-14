# Lab results

| Path | What it is |
|---|---|
| [r21/](r21/README.md) | **The reviewed evidence.** A full run copied from a named CI run, with its tested commit and source fingerprints. Cite from here. |
| [proofs/](proofs/README.md) | Default output directory of the standalone proof runner. Empty in the repository. |
| `matrix.json`, `smt.json`, `sensitivity.json`, `run.log` | The original oracle run, written by `python -m lab.oracle` in commit `41e7c39`, before the R21 runner existed. |

The top-level files predate the run manifest and record no source
fingerprints, so on their own they are not evidence about the current sources;
the current agreement gate rejects files without fingerprints (see
[reproduce.md](../docs/reproduce.md)). They are kept for two reasons:

- `lab/tests/test_evidence.py` loads `matrix.json` and `smt.json` as fixtures
  for the evidence gates, injecting the current fingerprints itself. Changing
  these files changes what those tests check.
- [method.md](../docs/method.md) and [oracle-audit.md](../docs/oracle-audit.md)
  cite `matrix.json` for the 80-cell result, which the reviewed R21 run
  reproduces.

`python -m lab.oracle` still writes here and overwrites them. For a fresh run
use `python -m lab.run --output out/<name>` instead, which writes a manifest and
never mixes partial and full evidence. See [lab/README.md](../README.md).
