# Security

## Reporting

Report suspected vulnerabilities through GitHub's private advisory form on this
repository ("Security" → "Report a vulnerability"). Please do not open a public
issue first.

There is no service to take offline and no fleet to patch, so there is no
embargo policy to negotiate — a fix and a public advisory will follow as
quickly as the work allows.

## Known and accepted limitations

These are properties of the current design, not undiscovered bugs. They do not
need reporting; if you think one of them is worse than described, that does.

**The MQTT transport is plaintext.** There is no TLS backend, and
`tls_enable = true` causes a refusal to start rather than a silent downgrade —
see [ADR 0008](docs/adr/0008-no-tls.md). Broker credentials and control
commands travel in the clear. Deploy on a network where that is acceptable, or
tunnel it.

**Control commands are only as trustworthy as the broker.** Anything able to
publish to the control topic can operate the appliance. Access control lives in
the broker; this program does not authenticate publishers.

**One process, no privilege separation.** The appliance path and the network
path share an address space — see [ADR 0005](docs/adr/0005-single-process.md).
A memory-safety defect in either reaches the other.

**No physical appliance has ever been connected to this code.** The wire-facing
code is exercised by a simulator written from the same reading of the standard.
Treat any claim about behaviour against real hardware as unverified.

## What the code does about it

The wire-facing layers are written on the assumption that every byte from the
peer is hostile, because a desynchronised stream can place any value anywhere:

- Packet length is read from the header and validated against both the bytes
  actually held and the reserved ranges before anything is decoded.
- Every parse entry point takes the caller's capacity as a parameter, so a
  length arriving from the wire is checked against the destination rather than
  trusted.
- Declared counts above the compile-time ceiling are refused outright rather
  than truncated. A truncated capability list looks plausible and is wrong.
- No allocation and no I/O in the core, so buffers are caller-supplied and
  bounded at sizes derived in one place.
- CI runs the tests under AddressSanitizer and UBSan in non-recovering mode.

## Configuration

`taiseia-bridge.conf` holds the broker password. Install it readable only by
its owner, and keep it out of version control — `.gitignore` blocks it by name.
