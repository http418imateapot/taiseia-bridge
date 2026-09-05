"""Run CBMC and distinguish proved properties, counterexamples and tool errors."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

from lab.evidence import ROOT

CHECKS = ["--bounds-check", "--pointer-check", "--pointer-overflow-check",
          "--conversion-check", "--signed-overflow-check", "--unsigned-overflow-check",
          "--div-by-zero-check", "--undefined-shift-check",
          "--unwind", "32", "--unwinding-assertions"]
METADATA = ["h_parse", "h_bad_line", "h_duplicate", "h_capacity", "h_config",
            "h_registration", "h_port", "h_no_metadata", "h_read", "h_write", "h_arguments"]


def verdict(returncode, document, demo=False):
    properties = [p for record in document for p in record.get("result", [])]
    statuses = [record["cProverStatus"] for record in document if "cProverStatus" in record]
    failed = [p for p in properties if p.get("status") == "FAILURE"]
    if any(p.get("status") not in ("SUCCESS", "FAILURE") for p in properties):
        return False
    if not demo:
        return returncode == 0 and statuses == ["success"] and bool(properties) and not failed
    # An executable missing, a timeout, parse failure or unrelated assertion is
    # not the paper's counterexample. Both intended signed conversions must fail.
    functions = set()
    for p in failed:
        location = p.get("sourceLocation", {})
        name = location.get("function", "")
        if name not in ("naive_sigma8", "naive_sigma16"):
            return False
        if "conversion" not in p.get("description", "").lower():
            return False
        functions.add(name)
    return (returncode == 10 and statuses == ["failure"] and
            functions == {"naive_sigma8", "naive_sigma16"})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cbmc", default="cbmc")
    parser.add_argument("--output", type=Path, default=ROOT / "lab/results/proofs")
    parser.add_argument("--suite", choices=("verify", "demo", "all"), default="all")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per harness")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        version = subprocess.run([args.cbmc, "--version"], capture_output=True,
                                 text=True, check=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError) as exc:
        print("CBMC unavailable: %s" % exc, file=sys.stderr)
        return 2
    harnesses = []
    if args.suite != "demo":
        harnesses += ["h_encode", "h_decode", "h_roundtrip"] + METADATA
    if args.suite != "verify":
        harnesses += ["h_naive"]
    summary = {"cbmc": version, "checks": CHECKS, "harnesses": [],
               "scope": "CBMC target model, bounded objects, shared datatype premise"}
    proof_dir = ROOT / "lab/proofs"
    for name in harnesses:
        sources = ["lab/proofs/" + name + ".c", "lab/proofs/reference.c",
                   "src/core/taiseia/general.c", "src/core/taiseia/datatype.c"]
        if name in METADATA:
            sources[0] = "lab/proofs/h_metadata.c"
            sources += ["lab/subject/metadata.c"]
        if name == "h_naive":
            sources = ["lab/proofs/h_naive.c", "lab/proofs/naive.c"]
        command = [args.cbmc] + sources + ["-Isrc", "-Ilab/proofs", "-Ilab/subject"] + CHECKS
        if name in METADATA:
            command += ["--function", name]
        command += ["--json-ui"]
        if name == "h_naive":
            command += ["--trace"]
        started = time.perf_counter()
        record = {"harness": name, "command": command, "expected_counterexample": name == "h_naive"}
        try:
            p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                               timeout=args.timeout, encoding="utf-8", errors="replace")
            (args.output / (name + ".json")).write_text(p.stdout, encoding="utf-8")
            (args.output / (name + ".stderr.log")).write_text(p.stderr, encoding="utf-8")
            record["exit_code"] = p.returncode
            document = json.loads(p.stdout)
            record["passed"] = verdict(p.returncode, document, name == "h_naive")
            properties = [v for item in document for v in item.get("result", [])]
            record["properties"] = len(properties)
            record["failed_properties"] = [v for v in properties if v.get("status") != "SUCCESS"]
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            record.update(passed=False, error=str(exc))
        record["elapsed_s"] = round(time.perf_counter() - started, 3)
        summary["harnesses"].append(record)
        (args.output / (args.suite + ".json")).write_text(
            json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print("%s: %s (%.3f s)" % (name, "PASS" if record["passed"] else "FAIL",
                                       record["elapsed_s"]), flush=True)
    return 0 if all(r["passed"] for r in summary["harnesses"]) else 1


if __name__ == "__main__":
    sys.exit(main())
