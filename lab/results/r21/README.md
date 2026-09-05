# Reviewed R21 evidence

This snapshot comes from the successful [CI run 33935095260](https://github.com/http418imateapot/taiseia-bridge/actions/runs/33935095260),
which tested commit `bcb17fc7bc3c73c0125f5e59452d10dd1511f914`.
All eight CI jobs passed. The source fingerprints in `manifest.json` were
checked against the local sources before archiving; the original run was clean.

The evidence commit that adds this directory is later than the tested source
commit. Results are not relabeled as testing that later commit.

## Established results

- Python enumeration and z3 agree on all 80 cells and all 40 bidirectional
  exactness checks. Only INT8 A/strict and B/wide lose values: 128 each, least
  witness -128. The lenient receiver is exact for all ten modeled types.
- The compiled C experiment agrees with those measured counts, exercises the
  production codec against the separate C reference, and checks all nine
  adapter/response combinations. It recorded 18,496,234 checks and zero failures;
  these are executed checks, not distinct test functions or proof properties.
- All three production-codec harnesses and eleven lab metadata entry points
  passed. The naive demonstration returned CBMC exit code 10, with the two
  required signed-conversion failures. The complete runner returned zero.
- `proofs/all.json` records CBMC 6.11.0, each command and each property count.
  Counts include CBMC-generated checks and may repeat across shared harness
  code. They are not independent scientific propositions and should not be summed
  to imply additional coverage. Recorded elapsed times are single-run timings.

## Files and integrity

Top-level logs, measured JSON and `proofs/all.json` are verbatim CI outputs.
The large raw `proofs/h_*.json` files are stored losslessly as `.json.gz` to keep
reviewable diffs small. `archive.json` records original and stored SHA-256 values.
For example:

```python
import gzip, json
from pathlib import Path
proof = json.loads(gzip.decompress(Path(
    "lab/results/r21/proofs/h_encode.json.gz").read_bytes()))
```

`regressions/negative-cast/` preserves the original failed encoder proof from
[CI run 33934868684](https://github.com/http418imateapot/taiseia-bridge/actions/runs/33934868684).
It demonstrates why the production encoder and independent reference were
changed to compute nonnegative residues before conversion. This historical
failure is intentional evidence, not a failed result of the final implementation.
The cast was defined in C99; it violated the enabled value-preserving conversion
check rather than establishing undefined behavior.

The metadata subject is confined to `lab/subject/`; no production metadata
integration or physical appliance is covered. See
[the R21 comparison](../../docs/paper-r21.md) for assumptions A1–A4.
