# ADR 0001 — The appliance link is a port, with an asynchronous contract

Status: accepted

## Context

The core needs to read and write appliance services. Three things must be able
to vary without the core noticing:

1. **The wire protocol.** TaiSEIA 101 over UART today. Modbus, or a vendor
   gateway, plausibly later.
2. **The concurrency model.** A desktop build can afford a worker thread. A
   single-threaded build, an RTOS task, and a bare-metal superloop with
   interrupt-driven UART cannot agree on one.
3. **Whether there is hardware at all.** The simulator has to be a peer of the
   real link, not a special case inside it.

A blocking `read_service()` would settle (1) but hard-code (2): every caller
would then assume it is allowed to block, and the bare-metal build could never
satisfy that.

## Decision

`ports/appliance_link.h` exposes a **submit / poll** pair. Neither call ever
blocks.

```c
result_t link_submit(link_t *l, const link_req_t *req);   /* RES_EBUSY if full */
result_t link_poll  (link_t *l, link_resp_t *out);        /* RES_EAGAIN if not ready */
```

The core drives it from its existing non-blocking loop and holds the retry and
scheduling policy itself. An adapter is free to satisfy the contract however it
likes:

| Adapter | submit | poll |
|---|---|---|
| threaded | enqueue; worker thread blocks on the port | dequeue a completed response |
| inline | store the request | advance a state machine one step |
| RTOS | post to a task queue | check the reply queue |
| bare metal | start an interrupt-driven transmission | inspect the ring buffer |

Threads are therefore an implementation detail of one adapter, not a core
concept. There is no thread port in `ports/`.

## Consequences

- The core stays free of any concurrency primitive, so it compiles and tests
  identically on every target.
- Retry policy lives in the core, where it is unit-testable, rather than being
  reimplemented per adapter.
- **The 200 ms deadline is the adapter's, not the core's.** This paragraph
  originally claimed both, and implementing the model showed that to be wrong.
  The deadline is measured from the last byte of the request leaving the wire,
  which only an adapter can know; timing it from `submit()` would include
  queueing delay on a threaded adapter and measure something else entirely. So
  adapters enforce it and report `RES_ETIMEOUT`, and the core owns everything
  above: how many times to retry, when to re-register, and when to declare the
  appliance offline. The core also keeps a much longer backstop deadline, so
  that an adapter which accepts a request and never answers cannot wedge it.
- The cost is that request state must be explicit rather than living on a
  blocked call stack. This is the same shape the MQTT client already uses, so
  it is not a new idea in this codebase.
- A caller wanting a simple blocking helper can build one over submit/poll. The
  reverse is not possible, which is why the asynchronous form is the primitive.

## Alternatives rejected

**Blocking `read_service()` plus a thread port.** Simpler to write and to read.
Rejected because it forces every target to have threads, which rules out the
bare-metal and cooperative-scheduler cases outright.

**Byte-level port only, with all framing in the core.** This is *also* done —
see `ports/serial.h` — but it is not sufficient on its own. Modbus does not
share TaiSEIA framing, so a Modbus adapter cannot be expressed as "core framing
over a serial port". Two seams at two levels are needed, not one.
