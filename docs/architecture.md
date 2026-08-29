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

## 4. Concurrency is not in the core

`appliance_link` is asynchronous by contract: `submit` and `poll`, neither of
which blocks. The core drives them from a loop that never blocks, and holds the
timeout and retry policy itself.

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

The appliance reports its own capabilities at registration — for each service:
the access direction, the data type code, and the supported range or bit mask.
That is enough to describe a service's *shape* without knowing its *meaning*.
Meaning lives in the per-appliance service tables, which are licensed material
and are not in this repository; consumers apply their own copy.

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
src/core/taiseia/   framing, checksums, data types, limits
src/core/util/      result type, and the small primitives built on it
src/ports/          interfaces: appliance_link, serial
src/adapters/sim/   a simulated appliance, including fault injection
src/adapters/       posix, win32                        (none today)
src/apps/           bridge, cli                         (none today)
tests/              host tests; no platform, no hardware
cmake/              the freestanding check
```

## 8. What is not built yet

Registration payload decoding, the capability model, MQTT, configuration, the
serial adapters, and both applications. The southbound framing layer and the
simulator are in place and tested; everything above them is not.

See [testing.md](testing.md) for what "tested" means here, and what it does not.
