"""Reproduce R21 experiments with explicit scope, logs and source provenance."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from lab.evidence import ROOT, digest_source


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "out/lab")
    parser.add_argument("--c-experiment", type=Path, help="compiled lab/subject/experiment.c")
    parser.add_argument("--cbmc", help="CBMC executable; requires a C preprocessor")
    parser.add_argument("--require-full", action="store_true", help="fail if C or proofs are skipped")
    parser.add_argument("--force", action="store_true", help="overwrite an existing run directory")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()) and not args.force:
        parser.error("output is not empty; choose a new directory or explicitly use --force")
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LAB_RESULTS_DIR=str(output), PYTHONUTF8="1")
    source_files = []
    for folder in ("lab", "src/core/taiseia", "scripts"):
        source_files += [p for p in (ROOT / folder).rglob("*")
                         if p.suffix in (".py", ".c", ".h", ".txt", ".ps1")
                         and "results" not in p.parts and "__pycache__" not in p.parts]
    source_files += [ROOT / ".github/workflows/ci.yml", ROOT / "lab/proofs/Makefile"]
    git_sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                             capture_output=True, check=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, text=True,
                           capture_output=True, check=True).stdout.strip() != ""
    manifest = {"schema": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
                "commit": git_sha, "dirty": dirty, "python": platform.python_version(),
                "platform": platform.system(), "machine": platform.machine(),
                "ci_run_url": ("https://github.com/%s/actions/runs/%s" %
                               (env["GITHUB_REPOSITORY"], env["GITHUB_RUN_ID"]))
                              if env.get("GITHUB_RUN_ID") else None,
                "source_hash_format": "sha256 with LF newlines",
                "sources": {p.relative_to(ROOT).as_posix(): digest_source(p)
                            for p in sorted(set(source_files))}, "stages": [], "status": "running"}

    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    def stage(name, command, timeout=3600, destination=None):
        start = time.perf_counter()
        record = {"name": name, "command": [str(x) for x in command]}
        # Do not commit local interpreter/install paths as research evidence.
        record["command"][0] = Path(record["command"][0]).name
        record["command"] = [arg.replace(str(ROOT), ".") for arg in record["command"]]
        print("Running " + name, flush=True)
        save()
        try:
            p = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True,
                               encoding="utf-8", errors="replace", timeout=timeout)
            (output / (destination or name + ".log")).write_text(p.stdout, encoding="utf-8")
            (output / (name + ".stderr.log")).write_text(p.stderr, encoding="utf-8")
            record.update(exit_code=p.returncode, status="passed" if p.returncode == 0 else "failed")
            if p.returncode:
                print(p.stderr[-3000:] or p.stdout[-3000:], file=sys.stderr)
        except (OSError, subprocess.SubprocessError) as exc:
            record.update(status="failed", error=str(exc))
        record["elapsed_s"] = round(time.perf_counter() - start, 3)
        manifest["stages"].append(record)
        save()
        if record["status"] != "passed":
            raise RuntimeError(name + " failed")

    try:
        stage("unit", [sys.executable, "-m", "unittest", "discover", "-s", "lab/tests", "-v"])
        stage("oracle", [sys.executable, "-m", "lab.oracle"])
        c_result = None
        if args.c_experiment:
            manifest["c_binary_sha256"] = hashlib.sha256(args.c_experiment.read_bytes()).hexdigest()
            stage("c-experiment", [str(args.c_experiment.resolve())], destination="c-experiment.json")
            c_result = output / "c-experiment.json"
        else:
            manifest["stages"].append({"name": "c-experiment", "status": "skipped"})
        command = [sys.executable, "-m", "lab.paper", "--results", str(output)]
        if c_result:
            command += ["--c-result", str(c_result)]
        stage("paper", command)
        if args.cbmc:
            stage("proofs", [sys.executable, "-m", "lab.proofs.run", "--cbmc", args.cbmc,
                             "--output", str(output / "proofs")], timeout=15000)
        else:
            manifest["stages"].append({"name": "proofs", "status": "skipped"})
        skipped = any(s["status"] == "skipped" for s in manifest["stages"])
        manifest["status"] = "partial" if skipped else "passed"
        if args.require_full and skipped:
            raise RuntimeError("full evidence required: C experiment or proofs were skipped")
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as exc:
        manifest["status"] = "failed"
        print(str(exc), file=sys.stderr)
    manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
    save()
    print("Evidence: %s (%s)" % (output, manifest["status"]))
    return 1 if manifest["status"] == "failed" else 0


if __name__ == "__main__":
    sys.exit(main())
