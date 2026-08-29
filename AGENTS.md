# AGENTS.md

Instructions for coding agents working in this repository. Humans should read
[docs/architecture.md](docs/architecture.md) first; this file assumes it.

## What this is

A TaiSEIA 101 (CNS 16014) appliance link with an MQTT northbound, in portable
C99. The program takes the **HNA role** from the standard: it is the requester
on a point-to-point UART link to a smart appliance (SA).

Targets: Linux, Windows, and embedded Linux. No external libraries at run time.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

Sanitizers are on by default on non-Windows builds and do not recover: an
undefined-behaviour report fails the build. Do not disable them to get a green
run.

There is no compiler on the primary development machine. CI is the build and
test environment — push and read the run. Never claim code compiles or passes
without a CI run to point at.

## Layering — the rule that matters most

```
src/core/     freestanding. No platform headers, no allocation, no I/O.
src/ports/    interfaces only. The core's view of the outside world.
src/adapters/ platform and device implementations of those interfaces.
src/apps/     wiring and argument parsing.
```

`src/core` and `src/ports` must not include `<unistd.h>`, `<sys/*>`,
`<netinet/*>`, `<arpa/*>`, `<windows.h>`, `<winsock*.h>`, `<pthread.h>`,
`<time.h>` or `<stdio.h>`, and must not call `malloc`, `calloc`, `realloc`,
`free`, `fopen` or `socket`.

This is checked by the `core_is_freestanding` CTest case
([cmake/check_freestanding.cmake](cmake/check_freestanding.cmake)). If you need
something from the platform, add a port — do not weaken the check.

**There is no thread port, and there must not be one.** Concurrency is an
adapter's private business; see [docs/adr/0001](docs/adr/0001-appliance-link-is-a-port.md).

## Conventions

- C99. Declarations at the start of a block. `/* */` comments.
- Every fallible function returns `result_t`. A function that can fail must not
  return `void`.
- Buffer sizes are derived in `src/core/taiseia/limits.h` and referenced by
  name. Never hand-compute a size next to the buffer it bounds.
- Every parse entry point takes the caller's capacity as a parameter. Length
  fields arriving from the wire are validated against both the bytes actually
  held and the destination's capacity, before anything is written.
- Prefer refusing malformed input over truncating it. A truncated capability
  list looks plausible and is wrong; a refusal is unambiguous.
- Naming: `ts_` for protocol code, `link_` for the appliance link port,
  `sim_link_` for the simulator.

## What must never enter this repository

- The CNS 16014 or CNS 16090 PDFs, or any text, table or figure copied from
  them. They are sold per-seat and watermarked per download. `.gitignore`
  blocks `*.pdf`; do not add exceptions.
- The per-appliance service tables (CNS 16014 Annex A) in any form — not as a
  header, not as data, not as a comment. Capabilities are queried from the
  appliance at run time. See [docs/adr/0003](docs/adr/0003-no-service-tables.md).
- Broker credentials, device identifiers from real deployments, vendor register
  maps, or install paths from any specific product.
- Any claim of conformance to, or certification against, either standard.

Citing a clause by number ("CNS 16014 §7.1.2.1 sets the response deadline at
200 ms") is fine and encouraged. Reproducing the clause is not.

## Tests

Add a test with the behaviour, in the same commit. Name the test function after
the property it establishes, not the function it calls — `read_request_is_six_bytes_with_ff_filler`,
not `test_build_read`.

The simulator ([src/adapters/sim](src/adapters/sim)) encodes and decodes real
packets rather than short-circuiting them. Keep it that way: it is the only
thing standing in for hardware, and a simulator that agrees with the core by
construction proves nothing.

## Commits and pull requests

- Conventional commits: `feat(core):`, `fix(sim):`, `docs:`, `test:`, `chore:`.
- Work on `develop`. `main` takes merges through pull requests.
- A pull request describes what was verified and what was not. This project has
  never run against a physical appliance; do not imply otherwise.
