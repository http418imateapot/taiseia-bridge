# ADR 0010 — Verified against a simulator, and said so plainly

Status: accepted

## Context

There is no physical appliance available to this project, and no protocol
analyser capture of one. There is also no compiler on the primary development
machine.

Code that has never been compiled, described as though it works, is a specific
and familiar failure. Avoiding it is worth some machinery.

## Decision

Two substitutes, and an explicit statement of what they are not.

**CI is the build environment.** Every push builds on Linux with
AddressSanitizer and UBSan in non-recovering mode, again on Linux without
sanitizers, and on Windows. No claim that something compiles or passes is made
without a run to point at.

**The simulator is the test fixture.** `src/adapters/sim` is a peer of the real
adapters: it encodes and decodes genuine packets rather than short-circuiting
them, so an exchange through it runs the same framing and checksum code a wire
would. It injects the faults hardware produces rarely and unrepeatably —
timeouts and corrupted checksums — on demand.

**The README says what has not been verified**, and pull requests say what they
did and did not establish.

## Consequences

- Everything above the wire can be developed and regression-tested today.
- Nothing here establishes that a real appliance behaves as the standard
  describes, that a USB-to-UART adapter meets the inter-byte timing, or that
  any particular device's registration payload parses. Those remain open.
- A simulator written from the same reading of the standard as the code shares
  that reading's mistakes. Building genuine packets narrows this but does not
  close it: a misread field is misread identically on both sides.
- When hardware appears, the simulator does not become useless — it becomes the
  fast test, and captured exchanges become fixtures replayed through the same
  port.

## Alternatives rejected

**Wait for hardware.** Leaves the portable ninety per cent unwritten and
unverified for an unbounded time.

**Write the code and mark it untested.** What the honesty is for; it is not a
substitute for the sanitizers and the CI matrix.
