# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
D1 -- how a sender puts a value into the two-byte data field.

CNS 16014 gives two rules for this field that are each other's business only
when the value is one byte wide and negative: negatives are two's complement,
and a one-byte value goes in the low byte with the high byte zero. Applied to
-3 the first gives FF FD and the second gives 00 FD. Both follow the text. A
sender picks one; the standard does not say which, and nothing on the wire
records the choice.

Two layouts, then, and they are the whole of D1:

    A  sign-extend   the field is one 16-bit two's-complement number
    B  zero-fill     the value is the low byte, the high byte is 00

For a two-byte type the value fills the field and the two coincide, which is
why the interesting cells are all in the one-byte rows.
"""

LAYOUTS = ("A", "B")

LAYOUT_LABEL = {
    "A": "sign-extend",
    "B": "zero high byte",
}


def encode(t, layout, v):
    """
    Equation (2): the byte pair layout `layout` writes for value `v` of type `t`.

    Returns (hi, lo). The caller is expected to have checked v is in V_t; a
    value outside the domain is not something a conforming sender emits and the
    matrix never asks for one.
    """
    if layout == "A":
        n = v % 65536
    elif layout == "B":
        n = v % 256 if t.value_len == 1 else v % 65536
    else:
        raise ValueError("unknown layout %r" % (layout,))

    return (n // 256, n % 256)
