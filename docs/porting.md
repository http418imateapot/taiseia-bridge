# Porting

This file exists so that "the adapters are swappable" is a checkable claim
rather than an aspiration. If adding a target needs a change under `src/core`,
that is a bug in the layering, not in the target.

## The two seams

```
core/model ──▶ ports/appliance_link.h ◀── adapters/taiseia_uart
                                      ◀── adapters/sim
                                      ◀── adapters/modbus
                                               │
adapters/taiseia_uart ──▶ ports/serial.h ◀── adapters/{posix,win32,...}
```

Pick the seam by what your target replaces:

- **A new operating system** — implement `ports/serial.h`. Around 150 lines.
- **A new appliance protocol** (Modbus, a vendor gateway) — implement
  `ports/appliance_link.h`. Framing is yours; do not try to reuse the TaiSEIA
  packet layer, which is a different wire format.
- **A new concurrency model** (RTOS, bare metal, cooperative) — also
  `ports/appliance_link.h`, because that is where the execution strategy lives.

## Implementing `ports/serial.h`

Five functions: open, close, read, write, flush. Nothing subtle, with three
exceptions.

**Port naming.** Passed through verbatim. On Windows, `COM1` through `COM9`
work bare, and `COM10` and above must be spelled `\\.\COM10` — the bare form
fails silently on a machine with enough USB adapters, which is a memorable
afternoon.

**Timeouts are per call, not per byte.** `serial_read` returns as soon as any
bytes arrive, or `RES_ETIMEOUT` at the deadline. It must not try to return a
whole packet: how many bytes a read returns says nothing about where a packet
ends. The caller loops.

**Latency, not throughput.** At 9600 bps a byte is about 1 ms and the standard
allows 30 ms between two bytes of one packet. A USB-to-UART adapter with a
16 ms latency timer fits, but not with much room; if a target proves marginal,
lowering the driver's latency timer is the first thing to try. Nothing in the
core needs to know.

## Implementing `ports/appliance_link.h`

Two functions, `submit` and `poll`, and **neither may block**. That is the
whole contract, and it is what keeps the core free of any concurrency
primitive. See [adr/0001](adr/0001-appliance-link-is-a-port.md).

| Strategy | `submit` | `poll` |
|---|---|---|
| threaded | enqueue; a worker thread blocks on the port | dequeue a completed reply |
| inline | store the request | advance a state machine one step |
| RTOS | post to a task queue | check the reply queue |
| bare metal | start an interrupt-driven transfer | inspect the ring buffer |

Return `RES_EBUSY` from `submit` when there is no room, and `RES_EAGAIN` from
`poll` when nothing is ready. Both are ordinary outcomes, not errors.

Do not implement retry or timeout policy in an adapter. The core owns it, so
that it is written once and tested once, and so that two adapters cannot
disagree about how many times three is.

`src/adapters/sim/sim_link.c` is the worked example, and is deliberately short.

## A Modbus adapter, concretely

Modbus is the most likely second appliance protocol, and it does **not** plug
into `ports/serial.h` — it has its own framing, so nothing in
`core/taiseia/frame.c` applies.

It plugs into `ports/appliance_link.h`, and it needs one thing this project
does not have: a mapping from register addresses to service identifiers. That
mapping is device-specific, is not standardised anywhere, and must be supplied
as configuration rather than compiled in. A register map embedded in source is
how a general-purpose bridge turns into a bridge for one product.

## Checklist for a new adapter

- [ ] Lives entirely under `src/adapters/<name>/`.
- [ ] Nothing under `src/core` or `src/ports` changed. If it did, say why in
      the pull request.
- [ ] `ctest` still passes, including `core_is_freestanding`.
- [ ] Added to the CI matrix, or the pull request says why it cannot be.
- [ ] Tested against the simulator where the seam allows it.
- [ ] Any device-specific data is configuration, not code.
