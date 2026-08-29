# Architecture

## 1. What the program is

The standard describes a chain of roles:

```
CS (cloud) / HG (home gateway)
      ↕   HAN: Wi-Fi, ZigBee, Z-Wave, Ethernet, PLC, PSTN
      ↕   TaiSEIA 101 network protocol            (CNS 16014 §6)
HNA (home network adaptor)
      ↕   UART, 9600 bps, point to point, request/response
      ↕   TaiSEIA 101 device monitoring protocol  (CNS 16014 §7)
SA (smart appliance)
```

This program is an **HNA**, with one substitution: northbound it speaks MQTT
rather than the §6 network protocol. That substitution is why it is not, and
does not claim to be, a conforming HNA.

Two properties of the southbound link shape everything else:

- **We are always the requester.** The appliance only ever answers. There is no
  deadline we must meet, only deadlines we must wait for.
- **The timing budget is generous.** An appliance begins responding within
  200 ms, retries are capped at three, and a gap over 30 ms between two bytes
  invalidates a packet. At 9600 bps a byte takes about 1 ms. A USB-to-UART
  adapter on a desktop operating system fits inside these numbers with room to
  spare, which is what makes a PC a plausible host.

## 2. Layers

```
                    ┌───────────────────────────────────┐
  inbound           │              core                 │        outbound
  adapters          │  ───────────────────────────────  │        ports
  ─────────         │  packet framing and checksums     │        ──────────────
  (mqtt control)    │  data types and value decoding    │  ports/appliance_link.h
  (cli)             │  capability model                 │  ports/serial.h
                    │  poll scheduling                  │
                    │  payload construction             │
                    └───────────────────────────────────┘
```

`src/core` and `src/ports` are freestanding: no platform headers, no
allocation, no I/O. The rule is enforced by a CTest case rather than by review,
because a layering rule nothing checks is a layering rule that has already been
broken somewhere you have not looked.

## 3. Two seams, at two levels

A single seam does not work here, and the reason is worth stating because it is
the most common way this design gets simplified into something that cannot do
its job.

**`ports/serial.h` — bytes.** Open a device, read, write, honour a timeout.
Roughly 150 lines per platform, with nothing subtle in it.

**`ports/appliance_link.h` — transactions.** Read a service, write a service,
ask the appliance to describe itself.

Northbound there is a third, `ports/socket.h`, on the same terms: nothing
blocks, and connecting is a loop of RES_EAGAIN rather than a call that stalls
the program against an unreachable broker.

Framing lives in the core, between the two, so it is written once and tested
once. But framing cannot be the *only* seam: Modbus does not share TaiSEIA's
packet layout, so a Modbus adapter cannot be expressed as "core framing over a
serial port". It plugs in at the transaction level instead.

```
core/model ──▶ ports/appliance_link.h ◀── adapters/taiseia_uart
                                       ◀── adapters/sim
                                       ◀── adapters/modbus      (none today)
                                                │
adapters/taiseia_uart ──▶ ports/serial.h ◀── adapters/{posix,win32}
```

All three southbound and northbound adapters now exist. `taiseia_uart` takes
the `inline` strategy from the table below -- a state machine advanced one step
per poll, no thread anywhere -- which is possible only because the framing
above it is pure code.

## 4. Concurrency is not in the core

`appliance_link` is asynchronous by contract: `submit` and `poll`, neither of
which blocks. The core drives them from a loop that never blocks.

Deadlines are split, and the split is not arbitrary. The 200 ms response
deadline is measured from the last byte of a request leaving the wire, which
only an adapter can know — timing it from `submit()` would include queueing
delay on a threaded adapter and measure the wrong thing. **Adapters enforce it
and report a timeout; the core owns retries, re-registration, and when to call
an appliance offline**, plus a far longer backstop so that an adapter which
accepts a request and never answers cannot wedge the model.

| Adapter | `submit` | `poll` |
|---|---|---|
| threaded | enqueue; a worker thread blocks on the port | dequeue a completed reply |
| inline | store the request | advance a state machine one step |
| RTOS | post to a task queue | check the reply queue |
| bare metal | start an interrupt-driven transfer | inspect the ring buffer |

So there is no thread port, and the core contains no concurrency primitive. A
blocking `read_service()` would have been easier to read and would have ruled
out the bottom two rows permanently. See
[adr/0001](adr/0001-appliance-link-is-a-port.md).

## 5. Raw identifiers, no translation

Published payloads carry TaiSEIA service identifiers and values as they arrive.
Nothing here maps service 3 to a name or value 1 to `"on"`.

The appliance reports its own capabilities at registration: for each service,
the access direction and its support data. That is usually enough to describe a
service's *shape* without knowing its *meaning*. Meaning lives in the
per-appliance service tables, which are licensed material and are not in this
repository; consumers apply their own copy.

There is one place this gets thinner than it sounds. An appliance using the
general record format sends no data type code, so its two support bytes could
be a range, a maximum or a bit mask and nothing on the wire says which. Those
capabilities are published as raw bytes. See
[mqtt-api.md](mqtt-api.md) §4.2 and [adr/0003](adr/0003-no-service-tables.md).

The practical consequence is that a new appliance type needs no change here.

## 6. Process model

One process. The appliance link may block for as long as the appliance takes,
without ever stalling the network side, because that is the adapter's problem
and not the core's.

A worst-case sweep of 32 services is about 20 s (200 ms × 3 retries, all
failing); a healthy one is under 2 s. That is comfortably inside a reporting
interval, which is what removes the argument for splitting the work across
processes.

## 7. Where things are

```
src/core/taiseia/   framing, checksums, data types, registration,
                    reassembly of fragmented replies, limits
src/core/model/     appliance state, capability cache, sweep scheduling, writes
src/core/mqtt/      MQTT 3.1.1 codec and client, topics, payloads, configuration
src/core/util/      result type, bounded string builder, JSON reader and writer
src/ports/          interfaces: appliance_link, serial, socket
src/adapters/sim/           a simulated appliance, with fault injection
src/adapters/taiseia_uart/  the appliance link over a real UART
src/adapters/posix/         sockets, serial, time
src/adapters/win32/         sockets, serial, time
src/apps/bridge/            the loop
src/apps/cli/               one exchange at a time, by hand
tests/              host tests; no platform, no hardware
cmake/              the freestanding check
```

## 8. What is not built yet

Nothing structural. What remains is either a protocol gap that is detected and
refused rather than half-handled, or a verification gap:

- **TLS.** Refused at startup rather than downgraded — see
  [adr/0008](adr/0008-no-tls.md).
- **`expand_bitfields`.** The setting is parsed and validated; nothing reads
  it.
- **No physical appliance** has ever been connected to any of this, and the
  **Windows serial adapter** has never carried a byte in CI — there is no
  virtual COM pair a hosted runner can create. See [testing.md](testing.md).

Both request layouts are implemented, both are polled, and a reply that arrives
in fragments is rejoined before anything above the transport sees it. What the
standard leaves open about a *registration* continuation is recorded as an
inference in `src/core/taiseia/reassembly.h` and in
[adr/0012](adr/0012-fragment-reassembly.md), and only a real appliance can
settle it.

See [testing.md](testing.md) for what "tested" means here, and what it does not.
