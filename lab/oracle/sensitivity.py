# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
Which of this project's transcription judgements change the answer?

`src/core/taiseia/datatype.c` is not a neutral copy of a published table. In at
least two rows the published lengths are internally inconsistent and somebody
had to decide, and the decision is now load-bearing for every test that decodes
a value of that type. One of the two decisions carries a NOTE explaining itself.
The other does not.

The question this asks is narrow and answerable: if a width had been decided the
other way, would the misreading matrix be different? A judgement that changes
nothing can stay a quiet line in a table. A judgement that moves cells is a
premise of the test suite and belongs in a comment where the next reader will
find it.

This is the method turned on the project rather than on the standard: the
matrix is not the result here, the SENSITIVITY of the matrix to a human decision
is.
"""

import copy
import json
import os
import sys
import time

from lab.oracle.layouts import LAYOUTS
from lab.oracle.readings import READINGS
from lab.oracle.sweep import exactness, misread
from lab.oracle.types import carryable_types
from lab.evidence import RESULTS



def profile(t):
    """The cells this type contributes: where it misreads, and where it is exact."""
    cells = {}
    for layout in LAYOUTS:
        for reading in READINGS:
            cells[(layout, reading)] = len(misread(t, layout, reading))

    exact = {}
    for reading in READINGS:
        over, under = exactness(t, reading)
        exact[reading] = (len(over), len(under))

    return cells, exact


def flipped(t):
    """The same type with the other plausible width."""
    other = copy.copy(t)
    other.value_len = 1 if t.value_len == 2 else 2
    return other


def run():
    started = time.time()
    findings = []

    for t in carryable_types():
        base_cells, base_exact = profile(t)
        alt = flipped(t)
        alt_cells, alt_exact = profile(alt)

        moved = sorted(k for k in base_cells if base_cells[k] != alt_cells[k])
        exact_moved = sorted(r for r in base_exact
                             if (base_exact[r][0] > 0, base_exact[r][1] > 0)
                             != (alt_exact[r][0] > 0, alt_exact[r][1] > 0))

        findings.append({
            "type": t.name,
            "code": t.code,
            "signed": t.signed,
            "width_implemented": t.value_len,
            "width_alternative": alt.value_len,
            "misread_cells_now": sum(1 for v in base_cells.values() if v),
            "misread_cells_if_flipped": sum(1 for v in alt_cells.values() if v),
            "cells_that_move": ["%s x %s" % k for k in moved],
            "exactness_that_moves": exact_moved,
            "sensitive": bool(moved) or bool(exact_moved),
        })

    return {
        "generated_by": "lab/oracle/sensitivity.py",
        "question": "does the misreading matrix depend on a width judgement?",
        "python": sys.version.split()[0],
        "findings": findings,
        "elapsed_s": round(time.time() - started, 2),
    }


def main():
    result = run()

    if not os.path.isdir(RESULTS):
        os.makedirs(RESULTS)
    path = os.path.join(RESULTS, "sensitivity.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, indent=2, sort_keys=True)
        f.write("\n")

    hot = [f for f in result["findings"] if f["cells_that_move"]]
    warm = [f for f in result["findings"]
            if f["exactness_that_moves"] and not f["cells_that_move"]]

    print("width judgements whose alternative MOVES a misreading cell:")
    for f in hot:
        print("  0x%02X %-11s %d -> %d bytes: %d misreading cells -> %d   (%s)"
              % (f["code"], f["type"], f["width_implemented"],
                 f["width_alternative"], f["misread_cells_now"],
                 f["misread_cells_if_flipped"],
                 ", ".join(f["cells_that_move"])))
    if not hot:
        print("  none")

    print("\nwidth judgements that move only an exactness verdict:")
    for f in warm:
        print("  0x%02X %-11s %d -> %d bytes: %s"
              % (f["code"], f["type"], f["width_implemented"],
                 f["width_alternative"], ", ".join(f["exactness_that_moves"])))
    if not warm:
        print("  none")

    insensitive = [f["type"] for f in result["findings"] if not f["sensitive"]]
    print("\ninsensitive to the judgement: %s"
          % (", ".join(insensitive) or "none"))
    print("\n%.2f s -> %s" % (result["elapsed_s"], os.path.relpath(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
