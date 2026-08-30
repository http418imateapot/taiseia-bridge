# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
The types the general data format can carry, read from the implementation.

The type-code table is a format-layer fact the bridge already holds, in
`src/core/taiseia/datatype.c`. This module reads that file rather than keeping
a second copy of it. Two reasons, and the first is the one that matters: an
oracle transcribed from the same document a second time can drift from the code
it is supposed to judge, and a drifted oracle is worse than none, because it
fails in the direction of agreeing with itself. The second is hygiene -- a
second copy of the table is a second thing to keep on the right side of the
licensing boundary.

Carryable means the value fits the general format's fixed two-byte data field:
a value length of one or two bytes. Anything wider is refused rather than
truncated, so it never reaches a decoder and has no layout question to answer.
"""

import os
import re

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))

DATATYPE_C = os.path.join(_REPO, "src", "core", "taiseia", "datatype.c")

# The one substitution the table's C needs before its lengths are arithmetic.
_DT_STAMP = 6

_ROW = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,"      # code
    r'\s*"([^"]*)"\s*,'               # name
    r"\s*([^,]+),"                    # value_len
    r"\s*([^,]+),"                    # support_len
    r"\s*(\w+)\s*,"                   # support kind
    r"\s*(true|false)\s*\}",          # is_signed
    re.DOTALL,
)


class Type(object):
    """One row of the type-code table."""

    def __init__(self, code, name, value_len, support_len, support, signed):
        self.code = code
        self.name = name
        self.value_len = value_len
        self.support_len = support_len
        self.support = support
        self.signed = signed

    @property
    def carryable(self):
        """Does a value of this type fit the general format's two bytes?"""
        return self.value_len in (1, 2)

    @property
    def lo(self):
        """Least value of the domain V_t."""
        if not self.signed:
            return 0
        return -(1 << (self.value_len * 8 - 1))

    @property
    def hi(self):
        """Greatest value of the domain V_t."""
        bits = self.value_len * 8
        if not self.signed:
            return (1 << bits) - 1
        return (1 << (bits - 1)) - 1

    def domain(self):
        return range(self.lo, self.hi + 1)

    def __repr__(self):
        return "Type(0x%02X, %s, %d, signed=%s)" % (
            self.code, self.name, self.value_len, self.signed)


def _length(expr):
    """Evaluate a length expression from the table's C."""
    s = expr.replace("DT_STAMP", str(_DT_STAMP))
    s = re.sub(r"(\d)[uU]\b", r"\1", s)
    s = s.strip()
    if not re.match(r"^[0-9+\s]+$", s):
        raise ValueError("unexpected length expression: %r" % expr)
    return eval(s)  # noqa: S307 -- charset asserted above


def _table_text(path):
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    start = src.index("k_types[] = {")
    depth = 0
    for i in range(src.index("{", start), len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                body = src[src.index("{", start) + 1:i]
                break
    else:
        raise ValueError("unterminated k_types table in %s" % path)

    return re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)


def all_types(path=DATATYPE_C):
    """Every row of the implementation's type-code table, in file order."""
    out = []
    for m in _ROW.finditer(_table_text(path)):
        out.append(Type(int(m.group(1), 16), m.group(2),
                        _length(m.group(3)), _length(m.group(4)),
                        m.group(5), m.group(6) == "true"))
    if not out:
        raise ValueError("no rows parsed from %s" % path)
    return out


def carryable_types(path=DATATYPE_C):
    """
    The types a general-format data field can hold.

    This is derived, not listed: whatever the implementation's table says is one
    or two bytes wide is what the general format can carry. If a row is added or
    a width corrected, the matrix below follows without anyone remembering to
    update it here.
    """
    return [t for t in all_types(path) if t.carryable]


if __name__ == "__main__":
    every = all_types()
    print("%d rows in %s" % (len(every), os.path.relpath(DATATYPE_C, _REPO)))
    print("%d carryable:" % len(carryable_types()))
    for t in carryable_types():
        print("  0x%02X  %-12s width %d  %-8s  V = [%d, %d]"
              % (t.code, t.name, t.value_len,
                 "signed" if t.signed else "unsigned", t.lo, t.hi))
