# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
Do the transcriptions agree, cell by cell?

This is the step the method turns on. Two transcriptions of the same prose,
written against different evaluators, are compared on every cell; any
disagreement is a defect in at least one of them and is reported before either
is used to judge an implementation.

It exits non-zero on any disagreement, so it can be the thing CI runs.
"""

import json
import os
import sys

RESULTS = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "results")


def _load(name):
    path = os.path.join(RESULTS, name)
    if not os.path.isfile(path):
        sys.stderr.write(
            "missing %s -- run `python -m lab.oracle.%s` first\n"
            % (os.path.relpath(path), name[:-5]))
        raise SystemExit(2)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _cells(doc):
    return {(c["type"], c["layout"], c["reading"]): c for c in doc["cells"]}


def _exact(doc):
    return {(e["type"], e["reading"]): e for e in doc["exactness"]}


def main():
    enum = _load("matrix.json")
    smt = _load("smt.json")

    problems = []

    a, b = _cells(enum), _cells(smt)
    if set(a) != set(b):
        problems.append("the two runs cover different cells: %s"
                        % sorted(set(a) ^ set(b)))
    for key in sorted(set(a) & set(b)):
        if a[key]["misread"] != b[key]["misread"]:
            problems.append(
                "%s layout %s x %s: enumeration says %d misread, z3 says %d"
                % (key[0], key[1], key[2],
                   a[key]["misread"], b[key]["misread"]))

    ea, eb = _exact(enum), _exact(smt)
    for key in sorted(set(ea) & set(eb)):
        if ea[key]["exact"] != eb[key]["exact"]:
            problems.append(
                "%s / %s: enumeration says exact=%s, z3 says exact=%s"
                % (key[0], key[1], ea[key]["exact"], eb[key]["exact"]))

    print("enumeration: %s, %.2f s" % (enum["method"], enum["elapsed_s"]))
    print("solver:      %s, %.2f s" % (smt["method"], smt["elapsed_s"]))
    print("%d cells and %d exactness checks compared"
          % (len(set(a) & set(b)), len(set(ea) & set(eb))))

    if problems:
        print("\nDISAGREEMENT -- at least one transcription is wrong:")
        for p in problems:
            print("  " + p)
        return 1

    print("every cell agrees")
    return 0


if __name__ == "__main__":
    sys.exit(main())
