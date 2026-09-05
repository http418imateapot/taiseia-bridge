# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
Equation (1) by exhaustion: every type, every layout, every reading, every value.

    F(t, s, r) = { v in V_t : dec_r(enc_s(v)) != v }

A cell is safe when F is empty. At this scale the domains are small enough that
exhaustion is a complete proof and not a sample -- the largest V_t here has
65,536 members -- which is worth saying plainly, because it is the reason the
solver in smt.py adds a second transcription rather than a stronger result.

Also checked here is the property that keeps the oracle from being vacuous: a
reading that accepts byte pairs no conforming sender would ever emit satisfies
"never misreads a conforming value" for the wrong reason. `low` is the example
-- it never misreads anything, and it also reads 12 FD as -3. So each reading is
asked a second question: is the set of pairs it accepts exactly the set some
layout can produce?
"""

import json
import os
import sys
import time

from lab.oracle.layouts import LAYOUTS, encode
from lab.oracle.readings import READINGS, decode
from lab.oracle.types import carryable_types
from lab.evidence import RESULTS, source_hashes



def misread(t, layout, reading):
    """F(t, s, r): the misread values, and how many there are."""
    bad = []
    for v in t.domain():
        if decode(t, reading, encode(t, layout, v)) != v:
            bad.append(v)
    return bad


def exactness(t, reading):
    """
    Does `reading` accept a byte pair if and only if some layout emits it?

    Both directions, and the second is easy to forget. A reading that accepts
    pairs no sender emits (`over`) satisfies "never misreads a conforming
    value" for the wrong reason -- it would read 12 FD as -3. A reading that
    rejects pairs a conforming sender does emit (`under`) refuses traffic it
    should have taken. Only a reading with neither is exact, and checking one
    direction alone reports three of the four readings here as exact when one
    is.
    """
    image = set()
    for layout in LAYOUTS:
        for v in t.domain():
            image.add(encode(t, layout, v))

    accepted = set()
    for hi in range(256):
        for lo in range(256):
            if decode(t, reading, (hi, lo)) is not None:
                accepted.add((hi, lo))

    return sorted(accepted - image), sorted(image - accepted)


def run():
    started = time.time()
    types = carryable_types()

    cells = []
    for t in types:
        for layout in LAYOUTS:
            for reading in READINGS:
                bad = misread(t, layout, reading)
                cells.append({
                    "type": t.name,
                    "code": t.code,
                    "width": t.value_len,
                    "signed": t.signed,
                    "layout": layout,
                    "reading": reading,
                    "domain": t.hi - t.lo + 1,
                    "misread": len(bad),
                    "witness": bad[0] if bad else None,
                })

    exact = []
    for t in types:
        for reading in READINGS:
            over, under = exactness(t, reading)
            exact.append({
                "type": t.name,
                "reading": reading,
                "exact": not over and not under,
                "over_accepted": len(over),
                "under_accepted": len(under),
                "over_witness": ("%02X %02X" % over[0]) if over else None,
                "under_witness": ("%02X %02X" % under[0]) if under else None,
            })

    return {
        "generated_by": "lab/oracle/sweep.py",
        "sources": source_hashes(),
        "method": "exhaustive enumeration",
        "python": sys.version.split()[0],
        "types": len(types),
        "layouts": list(LAYOUTS),
        "readings": list(READINGS),
        "cells": cells,
        "exactness": exact,
        "elapsed_s": round(time.time() - started, 2),
    }


def table(result):
    """The INT8 layout-by-reading counts, as the paper's Table 4."""
    lines = []
    by = {(c["layout"], c["reading"]): c["misread"]
          for c in result["cells"] if c["type"] == "INT8"}
    lines.append("INT8 misread counts   " + "".join("%9s" % r for r in READINGS))
    for s in LAYOUTS:
        lines.append("  layout %s            " % s
                     + "".join("%9d" % by[(s, r)] for r in READINGS))
    return "\n".join(lines)


def main():
    result = run()

    if not os.path.isdir(RESULTS):
        os.makedirs(RESULTS)
    path = os.path.join(RESULTS, "matrix.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, indent=2, sort_keys=True)
        f.write("\n")

    failing = [c for c in result["cells"] if c["misread"]]
    inexact = [e for e in result["exactness"] if not e["exact"]]

    print("%d cells  (%d types x %d layouts x %d readings)"
          % (len(result["cells"]), result["types"],
             len(LAYOUTS), len(READINGS)))
    print("%d cells misread; %d safe" % (len(failing),
                                         len(result["cells"]) - len(failing)))
    for c in failing:
        print("  %-6s layout %s x %-8s  %d of %d values, witness %d"
              % (c["type"], c["layout"], c["reading"],
                 c["misread"], c["domain"], c["witness"]))
    print()
    print(table(result))
    print()
    print("readings that are not exact (accept what no sender emits, or"
          " reject what one does):")
    for e in inexact:
        print("  %-6s %-8s  over %5d (%s)  under %4d (%s)"
              % (e["type"], e["reading"], e["over_accepted"],
                 e["over_witness"] or "-", e["under_accepted"],
                 e["under_witness"] or "-"))
    exact_for_all = [r for r in READINGS
                     if all(e["exact"] for e in result["exactness"]
                            if e["reading"] == r)]
    print("exact for all %d types: %s"
          % (result["types"], ", ".join(exact_for_all) or "none"))
    print()
    print("%.2f s -> %s" % (result["elapsed_s"], os.path.relpath(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
