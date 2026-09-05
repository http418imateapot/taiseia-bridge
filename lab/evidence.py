"""Shared output location and provenance; no equations live here."""

import hashlib
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = os.environ.get("LAB_RESULTS_DIR", str(ROOT / "lab" / "results"))


def digest_source(path):
    """SHA-256 of UTF-8 sources with LF newlines, independent of checkout OS."""
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def source_hashes():
    paths = sorted((ROOT / "lab" / "oracle").glob("*.py"))
    paths += [ROOT / "lab" / "evidence.py",
              ROOT / "src" / "core" / "taiseia" / "datatype.c"]
    return {p.relative_to(ROOT).as_posix(): digest_source(p)
            for p in paths}
