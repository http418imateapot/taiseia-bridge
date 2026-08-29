# ADR 0005 — One process; the concurrency model is a swappable strategy

Status: accepted

## Context

Splitting the appliance side and the network side into separate processes is a
real technique, and the argument for it is a slow, blocking appliance link that
would otherwise stall the network connection.

That argument is weak here. The link is 9600 bps request/response with a 200 ms
deadline and at most three retries, so a failing exchange costs about 600 ms and
a full sweep of 32 services costs about 20 s in the worst case, under 2 s
healthy. A separate process also has to be supervised, and supervision is the
least portable thing on any of these targets — procd, systemd and the Windows
service manager agree on nothing.

## Decision

One process.

The appliance link is free to block for as long as it likes, because that is
the adapter's business: the port contract is `submit` and `poll`, and neither
blocks. Whether the adapter achieves that with a worker thread, a state machine,
an RTOS task or an interrupt handler is invisible above it.

## Consequences

- One binary, one supervision story, no inter-process protocol to define,
  version and debug.
- Fault isolation between the two halves is lost. A crash in the appliance path
  takes the network path with it. Bounded buffers, the freestanding core and
  sanitizer-enforced tests are the compensation; a process boundary would have
  been a stronger guarantee and is honestly given up here.
- A local control socket for the command-line tool remains possible, but as a
  debugging convenience rather than a load-bearing seam.

## Alternatives rejected

**Two daemons with a local socket between them.** Buys fault isolation at the
cost of a protocol, a supervisor per platform, and a second thing to install.
Worth revisiting only if the appliance path proves unstable in the field.

**One process, one thread, non-blocking everywhere including the link.** Viable
on POSIX, where a serial descriptor is selectable, and awkward on Windows,
where it is not. Still reachable: it is precisely the `inline` adapter strategy.
