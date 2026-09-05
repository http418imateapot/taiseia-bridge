"""Check the R21 claims against measured oracle and optional C output."""

import argparse
import json
from pathlib import Path
import sys

from lab.evidence import RESULTS
from lab.oracle.agree import compare, indexed
from lab.oracle.types import carryable_types
from lab.oracle.layouts import LAYOUTS
from lab.oracle.readings import READINGS


def check_claims(enum, smt, implementation=None):
    problems = compare(enum, smt)
    types = carryable_types()
    groups = {
        "width_two": [t.name for t in types if t.value_len == 2],
        "width_one_unsigned": [t.name for t in types if t.value_len == 1 and not t.signed],
        "width_one_signed": [t.name for t in types if t.value_len == 1 and t.signed],
    }
    if (len(groups["width_two"]) != 8 or "INT16" not in groups["width_two"] or
            groups["width_one_unsigned"] != ["UINT8"] or groups["width_one_signed"] != ["INT8"]):
        problems.append("R21 Table 2 classification changed; review type premises")
    for cell in enum["cells"]:
        affected = cell["type"] == "INT8" and (cell["layout"], cell["reading"]) in {
            ("A", "strict"), ("B", "wide")}
        if cell["misread"] != (128 if affected else 0):
            problems.append("R21 P1/Table 4 mismatch: %s" % cell)
        if affected and cell["witness"] != -128:
            problems.append("R21 least counterexample changed")
    for row in enum["exactness"]:
        if row["reading"] == "lenient" and not row["exact"]:
            problems.append("R21 P3 fails: %s" % row)
    # R21's uniqueness is among the FOUR modeled receivers and requires both
    # interoperability and exactness. Low is also interoperable, but overaccepts.
    exact_receivers = [r for r in READINGS if all(e["exact"] for e in enum["exactness"]
                                                if e["reading"] == r)]
    if exact_receivers != ["lenient"]:
        problems.append("R21 unilateral exact receiver conclusion changed")
    if implementation is not None:
        keys = {(t.name, s, r) for t in types for s in LAYOUTS for r in READINGS}
        a = indexed(enum["cells"], ("type", "layout", "reading"), keys)
        b = indexed(implementation["cells"], ("type", "layout", "reading"), keys)
        for key in keys:
            for field in ("misread", "domain", "witness"):
                if a[key][field] != b[key][field]:
                    problems.append("C/%s differs for %s" % (field, key))
        keys = {(t.name, r) for t in types for r in READINGS}
        a = indexed(enum["exactness"], ("type", "reading"), keys)
        b = indexed(implementation["exactness"], ("type", "reading"), keys)
        for key in keys:
            for field in ("over_accepted", "under_accepted"):
                if a[key][field] != b[key][field]:
                    problems.append("C/%s differs for %s" % (field, key))
        if implementation["failures"] != 0 or implementation["checks"] <= 0:
            problems.append("C experiment failed or performed no checks")
        keys = {(s, b) for s in ("config", "registration", "null")
                for b in ("00 FD", "FF FD", "12 FD")}
        rows = indexed(implementation["responses"], ("source", "bytes"), keys)
        for (source, data), row in rows.items():
            raw = int(data.replace(" ", ""), 16)
            # result_t vocabulary is checked by the compiled experiment too.
            rc = 7 if source == "null" else (2 if data == "12 FD" else 0)
            value = raw if rc else -3
            if (row["result"], row["value"], row["raw"]) != (rc, value, raw):
                problems.append("R21 response mismatch: %s/%s" % (source, data))
    return groups, problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path(RESULTS))
    parser.add_argument("--c-result", type=Path)
    args = parser.parse_args(argv)
    try:
        enum = json.loads((args.results / "matrix.json").read_text(encoding="utf-8"))
        smt = json.loads((args.results / "smt.json").read_text(encoding="utf-8"))
        impl = json.loads(args.c_result.read_text(encoding="utf-8")) if args.c_result else None
        groups, problems = check_claims(enum, smt, impl)
        report = {"paper": "POWER2026 R21 (2026-08-30)", "groups": groups,
                  "cells": len(enum["cells"]), "exactness_checks": len(enum["exactness"]),
                  "c_experiment": "checked" if impl is not None else "not run",
                  "problems": problems, "passed": not problems,
                  "limitations": ["No physical appliance", "Metadata is a lab prototype",
                                  "Paper historical timings/counts are not acceptance targets",
                                  "Model assumptions A1-A4 remain unproved"]}
        (args.results / "paper.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 1 if problems else 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("paper: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
