# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
The same equations, transcribed a second time, for a solver.

At this scale the enumeration in sweep.py is already a complete proof -- the
largest domain has 65,536 members and every one of them is tried. The solver
adds no strength, and saying otherwise would be the most tempting dishonest
sentence in this directory.

What it adds is independence. The value of this file is that it is a SECOND
transcription of equations (2) to (4), written against a different evaluator,
by hand, from the same prose. When two independent transcriptions of an oracle
disagree, at least one is wrong, and the disagreement is visible before either
is used to judge an implementation. That is the only defence available against
an oracle that is confidently and consistently wrong -- which is the failure
this whole directory exists to catch, and which no amount of running the
implementation can detect.

It also keeps the method usable past the point where enumeration stops. Widen
the field, add a scaling layer, combine two fields, and the domains stop being
enumerable while the solver still answers.

Everything here is integer arithmetic: `%` and `/` on z3 Int are Euclidean for
a positive divisor, which is what the equations mean by mod and by the high
byte.
"""

import json
import os
import sys
import time

try:
    import z3
except ImportError:  # pragma: no cover -- reported by the caller
    raise

from lab.oracle.layouts import LAYOUTS
from lab.oracle.readings import READINGS
from lab.oracle.types import carryable_types
from lab.evidence import RESULTS, source_hashes

def checked_status(solver):
    """Unknown/timeout is neither UNSAT nor evidence of completeness."""
    status = solver.check()
    if status not in (z3.sat, z3.unsat):
        raise RuntimeError("SMT returned unknown: " + solver.reason_unknown())
    return status


def _encode(t, layout, v):
    """Equation (2) in z3: value -> (hi, lo)."""
    if layout == "A":
        n = v % 65536
    else:
        n = (v % 256) if t.value_len == 1 else (v % 65536)
    return n / 256, n % 256


def _sigma(x, bits, signed):
    if not signed:
        return x
    return z3.If(x >= (1 << (bits - 1)), x - (1 << bits), x)


def _decode(t, reading, hi, lo):
    """
    Equations (3) and (4) in z3.

    Returns (accepted, value). `value` is only meaningful where accepted.
    """
    field = 256 * hi + lo

    if reading == "wide" or t.value_len == 2:
        v = _sigma(field, 16, t.signed)
        return z3.And(v >= t.lo, v <= t.hi), v

    v = _sigma(lo, 8, t.signed)
    in_domain = z3.And(v >= t.lo, v <= t.hi)

    if reading == "low":
        ok = z3.BoolVal(True)
    elif reading == "strict":
        ok = hi == 0
    elif reading == "lenient":
        ok = z3.Or(hi == 0, z3.And(hi == 255, v < 0))
    else:
        raise ValueError("unknown reading %r" % (reading,))

    return z3.And(ok, in_domain), v


def misread(t, layout, reading, limit=None):
    """
    Every v in V_t for which the round trip does not return v.

    unsat on the first call is the proof that the cell is safe. Otherwise each
    model is one counterexample and is excluded before asking again, so the
    count is a count and not an estimate.
    """
    v = z3.Int("v")
    s = z3.Solver()
    s.add(v >= t.lo, v <= t.hi)

    hi, lo = _encode(t, layout, v)
    accepted, got = _decode(t, reading, hi, lo)
    s.add(z3.Not(z3.And(accepted, got == v)))

    found = []
    while checked_status(s) == z3.sat:
        x = s.model()[v].as_long()
        found.append(x)
        s.add(v != x)
        if limit is not None and len(found) >= limit:
            break
    return found


def not_exact(t, reading):
    """
    Byte pairs where `reading` and the layouts disagree about what is conforming.

    Two questions, asked separately, because they fail differently:
    over  -- accepted, though no layout emits it (the oracle is unsound)
    under -- emitted by a layout, but rejected (the oracle refuses valid traffic)
    """
    hi, lo = z3.Ints("hi lo")
    vA, vB = z3.Ints("vA vB")

    def bounds(s):
        s.add(hi >= 0, hi <= 255, lo >= 0, lo <= 255)

    def emitted():
        out = []
        for layout, var in (("A", vA), ("B", vB)):
            ehi, elo = _encode(t, layout, var)
            out.append(z3.Exists([var], z3.And(var >= t.lo, var <= t.hi,
                                               ehi == hi, elo == lo)))
        return z3.Or(*out)

    accepted, _ = _decode(t, reading, hi, lo)

    over = z3.Solver()
    bounds(over)
    over.add(accepted, z3.Not(emitted()))

    under = z3.Solver()
    bounds(under)
    under.add(emitted(), z3.Not(accepted))

    def witness(s):
        if checked_status(s) == z3.unsat:
            return None
        m = s.model()
        return "%02X %02X" % (m[hi].as_long(), m[lo].as_long())

    return witness(over), witness(under)


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
                    "unsat": not bad,
                    "witness": min(bad) if bad else None,
                })

    exact = []
    for t in types:
        for reading in READINGS:
            over, under = not_exact(t, reading)
            exact.append({
                "type": t.name,
                "reading": reading,
                "exact": over is None and under is None,
                "over_witness": over,
                "under_witness": under,
            })

    return {
        "generated_by": "lab/oracle/smt.py",
        "sources": source_hashes(),
        "method": "SMT (z3)",
        "z3": z3.get_version_string(),
        "python": sys.version.split()[0],
        "types": len(types),
        "cells": cells,
        "exactness": exact,
        "elapsed_s": round(time.time() - started, 2),
    }


def main():
    result = run()

    if not os.path.isdir(RESULTS):
        os.makedirs(RESULTS)
    path = os.path.join(RESULTS, "smt.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, indent=2, sort_keys=True)
        f.write("\n")

    unsat = [c for c in result["cells"] if c["unsat"]]
    print("z3 %s, %d cells" % (result["z3"], len(result["cells"])))
    print("%d cells proved safe (unsat); %d with counterexamples"
          % (len(unsat), len(result["cells"]) - len(unsat)))
    for c in result["cells"]:
        if not c["unsat"]:
            print("  %-6s layout %s x %-8s  %d values, least %d"
                  % (c["type"], c["layout"], c["reading"],
                     c["misread"], c["witness"]))
    print("exact for all %d types: %s"
          % (result["types"],
             ", ".join(r for r in READINGS
                       if all(e["exact"] for e in result["exactness"]
                              if e["reading"] == r)) or "none"))
    print("%.2f s -> %s" % (result["elapsed_s"], os.path.relpath(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
