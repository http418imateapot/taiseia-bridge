"""Fail closed on incomplete, stale, duplicated or contradictory evidence."""

import json
import os
import sys

from lab.evidence import RESULTS, source_hashes
from lab.oracle.layouts import LAYOUTS
from lab.oracle.readings import READINGS
from lab.oracle.types import carryable_types


def indexed(rows, fields, expected):
    result = {}
    for row in rows:
        key = tuple(row[field] for field in fields)
        if key in result:
            raise ValueError("duplicate evidence row: %s" % (key,))
        result[key] = row
    if set(result) != expected:
        raise ValueError("incomplete or unexpected evidence rows: %s" %
                         sorted(set(result) ^ expected))
    return result


def compare(enum, smt):
    types = carryable_types()
    cells = {(t.name, s, r) for t in types for s in LAYOUTS for r in READINGS}
    exact = {(t.name, r) for t in types for r in READINGS}
    for doc in (enum, smt):
        if doc.get("sources") != source_hashes():
            raise ValueError("stale evidence: source fingerprints do not match")
        if doc["types"] != len(types):
            raise ValueError("type count does not match the source table")
    a = indexed(enum["cells"], ("type", "layout", "reading"), cells)
    b = indexed(smt["cells"], ("type", "layout", "reading"), cells)
    problems = []
    for key in sorted(cells):
        for field in ("code", "width", "signed", "domain", "misread", "witness"):
            if a[key][field] != b[key][field]:
                problems.append("%s: %s disagrees" % (key, field))
        if b[key]["unsat"] != (b[key]["misread"] == 0):
            problems.append("%s: inconsistent UNSAT flag" % (key,))
    ea = indexed(enum["exactness"], ("type", "reading"), exact)
    eb = indexed(smt["exactness"], ("type", "reading"), exact)
    for key in sorted(exact):
        for direction in ("over", "under"):
            count = ea[key][direction + "_accepted"]
            enum_found = ea[key][direction + "_witness"] is not None
            solver_found = eb[key][direction + "_witness"] is not None
            if (count > 0) != enum_found or enum_found != solver_found:
                problems.append("%s: %s direction disagrees" % (key, direction))
        for row in (ea[key], eb[key]):
            expected = row["over_witness"] is None and row["under_witness"] is None
            if row["exact"] != expected:
                problems.append("%s: inconsistent exactness flag" % (key,))
    return problems


def main():
    try:
        with open(os.path.join(RESULTS, "matrix.json"), encoding="utf-8") as f:
            enum = json.load(f)
        with open(os.path.join(RESULTS, "smt.json"), encoding="utf-8") as f:
            smt = json.load(f)
        problems = compare(enum, smt)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("INVALID EVIDENCE: %s" % exc, file=sys.stderr)
        return 2
    if problems:
        print("DISAGREEMENT:\n" + "\n".join(problems), file=sys.stderr)
        return 1
    print("%d cells and %d exactness checks agree in both directions" %
          (len(enum["cells"]), len(enum["exactness"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
