# Testing

## What has been verified

Everything in `src/core` and `src/adapters/sim`, on every push, by CI:

| Job | What it adds |
|---|---|
| linux / gcc / asan+ubsan | AddressSanitizer and UBSan, non-recovering; warnings are errors |
| linux / gcc / release | optimised build, no sanitizers — different code, different bugs |
| windows / msvc | a second compiler and a second standard library |

`-fno-sanitize-recover=all` matters: without it UBSan prints a line into a log
nobody reads and the build stays green.

Two CTest cases run in each:

- `unit` — the assertions in `tests/`
- `core_is_freestanding` — [cmake/check_freestanding.cmake](../cmake/check_freestanding.cmake),
  which fails the build if `src/core` or `src/ports` grows a platform
  dependency

And then a smoke test: the whole program, against the simulator, with the
payloads going to the log. The unit tests prove the pieces; this proves they
were wired together, and on Linux it runs under AddressSanitizer so the
composed program is checked and not just its parts.

It has already earned its place. The unit tests were green while the bridge
published `"online":true` with an empty attribute set, because it reported the
moment the appliance was identified rather than once anything had been read
from it. Nothing that tests one piece at a time was ever going to notice.

## What has not been verified

**No physical appliance has ever been connected to this code.** Nothing here
establishes that:

- a real appliance behaves as the standard describes;
- a USB-to-UART adapter meets the 30 ms inter-byte ceiling on any particular
  operating system;
- any specific device's registration payload parses;
- the serial adapters work at all -- nothing in CI opens a serial port, so
  they are exercised only through the port interface by a fake;
- the MQTT client can talk to a real broker;
- the protocol has been read correctly at all.

That last one deserves saying twice. The simulator is written from the same
reading of the standard as the code it exercises, so a field misread in one is
misread identically in the other, and the tests pass. Building genuine packets
on both sides narrows the gap — a framing error that only one side makes will
show up — but agreement between two things derived from one reading is not
evidence about the wire.

## The simulator

`src/adapters/sim` is a peer of the real adapters, not a shortcut around them.
It builds a reply as bytes, checksums it, and hands it back through the parser,
so every exchange runs the same framing code a wire would.

It injects what hardware produces rarely and unrepeatably:

```c
sim_link_drop_next(&sim, 2);   /* the next two requests time out    */
sim_link_corrupt_next(&sim);   /* the next reply has a bad checksum */
sim_link_set_down(&sim);       /* as a pulled cable                 */
```

Keep it honest. A simulator that answers from a table without encoding anything
would make the tests faster and prove nothing.

## Conventions

Name a test function after the property it establishes, not the function it
calls:

```c
static void reserved_lengths_are_refused(void);          /* yes */
static void signed_values_are_sign_extended(void);       /* yes */
static void test_ts_parse(void);                         /* no  */
```

Checks do not abort. One failure should not hide the next twenty — the pattern
across failures usually says more than the first one alone.

Test the refusals, not only the acceptances. Most of what a protocol decoder
owes its caller is the malformed input it declines: a length that disagrees with
the buffer, a reserved length treated as a size, a checksum that does not match.

## Running it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

## When hardware arrives

The simulator becomes the fast test rather than the only one. Captured
exchanges become fixtures replayed through the same port, and the conformance
test cases in CNS 16090 §6 — which cover exactly the HNA role this program
takes — become the outline for a hardware test plan.

Until then, [ADR 0010](adr/0010-verification-without-hardware.md) is the
standing position: say what was verified, and say what was not.
