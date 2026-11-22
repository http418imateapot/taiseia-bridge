# Testing

## What has been verified

Everything in `src/core` and `src/adapters/sim`, on every push, by CI:

| Job | What it adds |
|---|---|
| linux / gcc / asan+ubsan | AddressSanitizer and UBSan, non-recovering; warnings are errors; the pseudo-terminal tests; the broker integration test |
| linux / gcc / release | optimised build, no sanitizers — different code, different bugs |
| windows / msvc | a second compiler and a second standard library; the Windows serial adapter's argument checks and failure paths, on a real Windows machine |

Two further jobs check the expected values rather than the program; they are
described under "Beyond testing" below:

| Job | What it adds |
|---|---|
| linux / oracle and proofs (`verify`) | the Python oracle and z3; the lab C experiment compiled with gcc, sanitizers and `-Werror`; the CBMC proofs; uploads `r21-evidence` |
| lab / {ubuntu, windows} / Python {3.9, 3.14} | the lab's Python regression tests, through the same installer a contributor uses |

`-fno-sanitize-recover=all` matters: without it UBSan prints a line into a log
nobody reads and the build stays green.

Two CTest cases run in each of the three program jobs:

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
- the **Windows** serial adapter can carry a byte. Its argument checks, its
  refusal of a handle that is not a serial port, and the state of a closed port
  now run on a real Windows machine in CI; the byte-carrying half does not, and
  cannot. See "The Windows serial gap" below;

- the protocol has been read correctly at all.

That last one deserves saying twice. The simulator is written from the same
reading of the standard as the code it exercises, so a field misread in one is
misread identically in the other, and the tests pass. Building genuine packets
on both sides narrows the gap — a framing error that only one side makes will
show up — but agreement between two things derived from one reading is not
evidence about the wire.

**It has happened.** The multi-byte header was transcribed with the service id
in the low seven bits of byte 2 and a one-byte reserved field. CNS 16014:2025
tables 14 and 15 give byte 2 to the access bit alone, the service id two bytes
of its own, and the reserved field two — so from the reserved field on, every
offset was three bytes short, and multi-byte registration records (table 24)
were two bytes short. Every test passed: the simulator builds its replies with
the same functions it is meant to check, and the tests that wrote bytes by hand
wrote the same misreading. It was found by reading the tables against the code,
not by anything in CI.

The multi-byte tests now write their packets out by hand at the tables'
offsets, so a builder and a parser that agree on a wrong layout cannot pass
together. That makes those tests a second transcription, not a check against
the wire; only an appliance can be that.

## The broker integration test

`scripts/integration-broker.sh` starts a real mosquitto, runs the bridge
against it, and checks what the broker actually delivered.

This is the part a fake socket cannot reach, and the reason is not subtle: a
fake agrees with whatever the client does, *including whatever it does wrong*.
Mosquitto does not. It will reject a malformed CONNECT, ignore a subscription
it did not understand, and publish a last will exactly when the specification
says to -- none of which our own tests could establish about our own client.

Phase A is a clean run: connect, subscribe, report, take a command that is half
accepted and half refused, watch the written value appear in the next report,
and say goodbye on the way out. It also checks that a subscriber arriving after
the device has gone still learns what it was, which is what retaining
capabilities is for.

Phase B kills the bridge with no chance to say anything, so whatever the broker
publishes next is the will handed to it at CONNECT.

**It found two defects on its first run**, both invisible to everything else:

- **Capabilities were never published.** Not once, on any run. `mqttc_publish`
  reserved room for the largest packet imaginable rather than the one it was
  given, so the second of two publishes in one pass was refused every time --
  and the bridge reports telemetry and capabilities together.
- **The status transitions at startup were lost.** They happen while the broker
  connection is still being made, and since status is only published on change,
  nothing ever sent it again. The broker never saw the device come online.

Both had passed every unit test, because the fake never had an opinion about
what should have arrived.

## The pseudo-terminal tests

On POSIX, `tests/test_serial_pty.c` opens a real serial device and drives the
adapter through it.

A pseudo-terminal is a real tty: it has a line discipline, it echoes by
default, and it will translate carriage returns if you let it. That is what
makes it the right stand-in -- not that it resembles a USB adapter
electrically, but that it has all the same ways of quietly corrupting a binary
protocol, and turning those off is precisely what the adapter's setup code is
for.

What this reaches and a fake cannot:

- **Echo is off.** Left on, every byte we wrote would come back as though the
  appliance had said it, and every reply after that would be read at the wrong
  offset.
- **0x0D, 0x0A, 0x03 and 0x1A pass through untouched.** In cooked mode the
  first would arrive as the second, and the third would raise a signal.
- **A whole exchange over the device**, with an appliance played by the other
  end of the pair.
- **A silent peer times out on the protocol's schedule**, measured against a
  real clock.

Windows has no equivalent, which is the subject of the next section.

## The Windows serial gap

**This one is open, and it is not closing in CI.** Saying so is the point of
this section.

A Windows virtual COM pair needs a kernel-mode driver. Every free one --
com0com and its forks -- ships test-signed, so using it means enabling test
signing and rebooting, and a hosted runner cannot reboot in the middle of a
job. The commercial alternatives are not something an open-source project can
require of a contributor. There is no user-mode substitute: a named pipe opens
happily under `CreateFile`, but `GetCommState` fails on it, which is exactly
the behaviour the adapter relies on to reject a handle that is not a port.

So `tests/test_serial_win32.c` runs what needs no port, on a real Windows
machine, against the real adapter:

- **Bad line settings are refused before anything is opened** -- seven data
  bits, two stop bits, a baud rate outside the range the standard allows.
- **An absent port is reported absent**, not as a transport failure. One is a
  typo in a configuration file and the other is a cable; a caller that cannot
  tell them apart cannot say anything useful to whoever is holding the cable.
- **A handle that opens but is not a serial port is refused and released.**
  Every operation afterwards fails, which is how we know it was let go rather
  than kept half-configured.
- **A closed port refuses work, and closing twice is safe.**

What that leaves unverified is everything that touches the wire: whether
`SetCommState` really applies 9600 8N1, whether the `MAXDWORD` read-interval
timeout really makes reads return immediately, whether `PurgeComm` really
discards, and whether a whole transaction survives the round trip.

**How to close it.** With a com0com pair, or two USB adapters and a null-modem
cable, name the pair and the full exchange runs:

```
set TSB_TEST_COM_PAIR=COM11,COM12
ctest --test-dir build --output-on-failure
```

That test is written and in the repository; it skips, loudly, when the variable
is unset. Anyone who runs it against real ports and reports the result closes
a gap that CI cannot.

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

On Windows, build with `--config Debug` and pass `-C Debug` to `ctest`.

## Beyond testing: are the expected values right?

Everything above establishes that this program does what the tests expect. It
cannot establish that what the tests expect is right — an assertion compares an
output against a value a person transcribed from a document, and where the
document permits more than one reading, the assertion encodes whichever one that
person picked and passes forever.

That question has its own directory. [lab/](../lab/README.md) checks expected
values: it inventories the places where this project's are decisions rather than
facts, writes the contested ones as arithmetic, and checks each oracle two
independent ways before anything is judged by it. It ships nothing and nothing
in `src/` depends on it. Its Python half runs on a machine with no compiler; its
C experiment and proofs compile only in the `verify` CI job, and a local run
reports them as skipped rather than passed.

`c20ba30`, `d87aef4` and `b5b54b1` in this repository's history are all this
failure mode: assertions that passed, or would have passed, without establishing
the property. Two were caught by MSVC disagreeing with gcc rather than by the
assertions. See [verification.md](verification.md).

## When hardware arrives

The simulator becomes the fast test rather than the only one. Captured
exchanges become fixtures replayed through the same port, and the conformance
test cases in CNS 16090 §6 — which cover exactly the HNA role this program
takes — become the outline for a hardware test plan.

Until then, [ADR 0010](adr/0010-verification-without-hardware.md) is the
standing position: say what was verified, and say what was not.
