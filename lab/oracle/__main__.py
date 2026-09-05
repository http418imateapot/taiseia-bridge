# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
Run the whole oracle check: enumerate, solve, compare, then test the premises.

`python -m lab.oracle` from the repository root. Exits non-zero if the two
transcriptions disagree anywhere, which is what makes it usable as a gate.
"""

import sys

from lab.oracle import agree, sensitivity, sweep


def _banner(title):
    print("\n=== %s %s" % (title, "=" * max(0, 68 - len(title))))


def main():
    _banner("enumerate")
    sweep.main()

    _banner("solve")
    try:
        from lab.oracle import smt
        smt.main()
    except ImportError:
        print("z3 is not installed; skipping the second transcription.")
        print("  pip install z3-solver")
        print("SKIPPED -- the enumeration alone is unchecked against anything.")
        return 1

    _banner("compare")
    rc = agree.main()
    if rc:
        return rc

    _banner("premises")
    sensitivity.main()

    return rc


if __name__ == "__main__":
    sys.exit(main())
