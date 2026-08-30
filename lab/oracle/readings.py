# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
"""
D2 -- how a receiver reads a value out of the two-byte data field.

Four rules, differing only in what they do with the bytes above the value:

    wide     read the whole field as one number, then range-check it
    low      read the low bytes, ignore whatever is above them
    strict   read the low bytes, require everything above to be 00
    lenient  read the low bytes, require everything above to be 00, or FF
             when the value read is negative

TRANSCRIPTION NOTE, and it is a decision rather than a reading of the text.
The standard's two sentences are about a one-byte value in a two-byte field, so
the four rules are stated for one-byte types. Applying them to the matrix means
saying what they do for a two-byte type as well. The reading taken here is that
D2 is a rule about the padding -- the bytes above the value -- so for a
two-byte type, where there is no padding, all four coincide with "read the
field". The alternative, that `low` means "read one byte" whatever the type,
would make `low` misread every two-byte value above 255, which is not a rule
any implementer would write down. It is recorded here because the difference is
invisible in the result and someone re-deriving the matrix should not have to
guess which was meant.

`None` is refusal. A refusal counts as a misread: the value did not survive.
"""

READINGS = ("wide", "low", "strict", "lenient")

READING_LABEL = {
    "wide": "read the whole field",
    "low": "read the low byte, ignore the high",
    "strict": "read the low byte, high must be 00",
    "lenient": "read the low byte, high may be 00 or FF when negative",
}


def _signed(x, bits, signed):
    """sigma_w: read an unsigned w-bit number as two's complement when signed."""
    if signed and x >= (1 << (bits - 1)):
        return x - (1 << bits)
    return x


def decode(t, reading, b):
    """
    Equations (3) and (4): what `reading` gets out of byte pair `b` for type `t`.

    Returns the value, or None for a refusal.
    """
    hi, lo = b
    field = 256 * hi + lo

    if reading == "wide":
        v = _signed(field, 16, t.signed)
        return v if t.lo <= v <= t.hi else None

    if t.value_len == 2:
        # No padding: the value is the field, and all four rules agree.
        v = _signed(field, 16, t.signed)
        return v if t.lo <= v <= t.hi else None

    pad = hi
    v = _signed(lo, 8, t.signed)

    if reading == "low":
        pass
    elif reading == "strict":
        if pad != 0x00:
            return None
    elif reading == "lenient":
        if not (pad == 0x00 or (pad == 0xFF and v < 0)):
            return None
    else:
        raise ValueError("unknown reading %r" % (reading,))

    return v if t.lo <= v <= t.hi else None
