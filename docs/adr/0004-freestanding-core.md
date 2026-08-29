# ADR 0004 — The core is freestanding, and something checks it

Status: accepted

## Context

The project targets Linux, Windows and embedded Linux, and wants to keep an
RTOS and bare-metal target reachable. Everything portable is worth writing once;
everything platform-specific is worth confining.

Layering rules stated only in documentation decay quietly. One `#include
<unistd.h>` added to fix something urgent is invisible in review, and by the
time the Windows build breaks the dependency has relatives.

## Decision

`src/core` and `src/ports` are freestanding. They may not include
`<unistd.h>`, `<sys/*>`, `<netinet/*>`, `<arpa/*>`, `<windows.h>`,
`<winsock*.h>`, `<pthread.h>`, `<time.h>` or `<stdio.h>`, and may not call
`malloc`, `calloc`, `realloc`, `free`, `fopen` or `socket`.

`cmake/check_freestanding.cmake` scans for exactly that and runs as the CTest
case `core_is_freestanding`. A violation fails the build the way a failing
assertion does.

Needing something from the platform means adding a port, not an exception.

## Consequences

- The core compiles identically everywhere, so a test run on one platform says
  something about all of them.
- No allocation in the core means every buffer is caller-supplied and bounded
  at a size derived in `limits.h`. Refusing oversized input is then a local
  decision rather than a global policy.
- Banning `<stdio.h>` costs the convenience of a quick `printf` while
  debugging. Logging will arrive as a port. This is deliberate: a debugging
  `printf` that survives into a commit is exactly the dependency this rule
  exists to catch.
- The check is textual and can be fooled. It is a ratchet against drift, not a
  security boundary, and is worth what it costs precisely because it is cheap.

## Alternatives rejected

**State the rule in the contributing guide.** What this project did before, in
effect, and what produced the situation the rule now prevents.

**Enforce by linking the core against a stub libc.** Stronger, and far more
build machinery than a repository of this size can carry.
