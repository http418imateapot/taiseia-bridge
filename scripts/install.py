"""Discover local Python interpreters, select one and build a private lab venv."""

import argparse
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROBE = ("import json,sys,venv; print(json.dumps([sys.executable, "
         "list(sys.version_info[:3]),sys.prefix != sys.base_prefix]))")


def probe(path):
    try:
        p = subprocess.run([str(path), "-I", "-c", PROBE], capture_output=True,
                           text=True, timeout=10, check=True)
        executable, version, is_venv = json.loads(p.stdout)
        if Path(executable).stem.lower().startswith("pythonw"):
            return None
        if tuple(version) >= (3, 9) and not is_venv:
            return {"path": executable, "version": version}
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return None


def discover():
    candidates = [sys.executable]
    for folder in os.get_exec_path():
        candidates += glob.glob(str(Path(folder) / "python*"))
    if os.name == "nt":
        local = os.environ.get("LOCALAPPDATA")
        if local:
            candidates += glob.glob(str(Path(local) / "Programs/Python/*/python.exe"))
        launcher = shutil.which("py")
        if launcher:
            p = subprocess.run([launcher, "-0p"], capture_output=True, text=True,
                               timeout=10)
            candidates += re.findall(r"([A-Za-z]:\\[^\r\n]*?python.exe)",
                                     p.stdout + p.stderr, flags=re.I)
    found = {}
    for path in dict.fromkeys(candidates):
        if (not Path(path).is_file() or "windowsapps" in str(path).lower()
                or Path(path).stem.lower() == "pythonw"):
            continue
        item = probe(path)
        if item:
            found[os.path.normcase(str(Path(item["path"]).resolve()))] = item
    return list(found.values())


def choose(items, selection):
    if not selection.isdigit() or not 1 <= int(selection) <= len(items):
        raise ValueError("Choose a listed interpreter number.")
    return items[int(selection) - 1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python", help="explicit interpreter path (CI/unattended)")
    parser.add_argument("--list", action="store_true", help="list interpreters only")
    parser.add_argument("--dry-run", action="store_true", help="show commands only")
    parser.add_argument("--reuse", action="store_true", help="reuse matching .venv")
    args = parser.parse_args(argv)
    try:
        if args.python:
            selected = probe(args.python)
            if selected is None:
                raise ValueError("Interpreter must be a base Python >= 3.9 with venv.")
            items = [selected]
        else:
            items = discover()
        for i, item in enumerate(items, 1):
            print("%d. Python %s  %s" %
                  (i, ".".join(map(str, item["version"])), item["path"]))
        if not items:
            raise ValueError("No usable Python found. Install Python >= 3.9 with venv.")
        if args.list:
            return 0
        selected = items[0] if args.python else choose(items, input("Python number: "))
        dest = ROOT / ".venv"
        exe = dest / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        if dest.exists():
            if not args.reuse:
                raise ValueError(".venv already exists; use --reuse for the same interpreter.")
            cfg = dest / "pyvenv.cfg"
            if not cfg.is_file() or not exe.is_file():
                raise ValueError("Existing .venv is incomplete; preserve it and resolve manually.")
            p = subprocess.run([str(exe), "-I", "-c", "import sys; print(sys._base_executable)"],
                               check=True, text=True, capture_output=True, timeout=10)
            if Path(p.stdout.strip()).resolve() != Path(selected["path"]).resolve():
                raise ValueError("Existing .venv uses a different interpreter.")
        commands = [] if dest.exists() else [[selected["path"], "-m", "venv", str(dest)]]
        commands.append([str(exe), "-m", "pip", "install", "--disable-pip-version-check",
                         "-r", str(ROOT / "lab" / "requirements.txt")])
        for command in commands:
            print(json.dumps(command), flush=True)
            if not args.dry_run:
                subprocess.run(command, check=True, cwd=ROOT)
        return 0
    except (EOFError, OSError, ValueError, subprocess.SubprocessError) as exc:
        print("install: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
