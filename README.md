# taiseia-bridge

A TaiSEIA 101 (CNS 16014) appliance link with an MQTT northbound, in portable
C99.

The program takes the **HNA role** defined by the standard: it is the requester
on a point-to-point UART link to a smart appliance (SA), reads the appliance's
capabilities and operating state, accepts control commands, and publishes
everything to an MQTT broker.

```
        MQTT broker
             ▲
             │  configurable topic templates, JSON payloads
      ┌──────┴────────┐
      │ taiseia-bridge │   ← HNA role
      └──────┬────────┘
             │  UART 9600 8N1, request/response, half duplex
        [ appliance (SA) ]
```

It is meant to run on a Linux box, on a Windows PC with a USB-to-UART adapter,
or on an embedded Linux target. No external libraries at run time: standard C99
plus what the platform already provides.

---

## Status

| | |
|---|---|
| Packet framing, checksums, data types | built, tested |
| Appliance link port and simulator | built, tested |
| Registration and capability decoding | built, tested |
| Appliance model, sweep scheduling, writes | built, tested |
| MQTT codec, topics, payloads, configuration | built, tested |
| MQTT client, socket port | built, tested |
| Platform adapters: sockets, serial, time, logging | built, tested |
| TaiSEIA UART link adapter | built, tested |
| The two applications | built, smoke-tested |
| Serial adapters (POSIX, Windows) | not started |

**No physical appliance has ever been connected to this code**, and no real
broker. Verification is by CI — Linux with AddressSanitizer and UBSan, Linux
release, and MSVC — by a simulator that encodes and decodes real packets, by
real loopback sockets on both platforms, by a real serial device on POSIX, and by a real MQTT broker.
None of that is evidence about an actual appliance.
See [docs/testing.md](docs/testing.md).

---

## Scope, and what this project deliberately does not do

Payloads carry **raw TaiSEIA service identifiers and values**. Nothing here
translates service id `3` into a name, or value `1` into `"on"`.

That is not an omission. Capabilities — each service's access direction, data
type, and supported range or bit mask — are **queried from the appliance at run
time** through the standard's registration exchange. Interpreting what a service
*means* requires the per-appliance service tables in CNS 16014 Annex A, which
are licensed material. Consumers apply their own copy.

The practical benefit: a new appliance type needs no change here.

See [ADR 0003](docs/adr/0003-no-service-tables.md).

---

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

## Running it, with no hardware and no broker

```bash
./build/taiseia-bridge --link sim -c examples/config/simulator.conf
```

With no broker configured the payloads go to the log instead of a socket, so
the whole path is visible:

```
info  taiseia-bridge 0.1.0-dev starting as SIM-0001
info  v1/from/device/SIM-0001/status {"device_id":"SIM-0001",...,"online":true}
info  v1/from/device/SIM-0001/telemetry {...,"sa_id":4,"online":true,
      "attributes":{"0":1,"3":55,"7":62}}
info  v1/from/device/SIM-0001/capabilities {...,"format":"general",
      "make":"SIMULATOR","model":"SIM-0001","capabilities":[...]}
```

One exchange at a time, through the same adapter the bridge uses:

```bash
./build/taiseia-cli --link sim identify
```

```bash
./build/taiseia-cli --port /dev/ttyUSB0 read 4 7
```

## Running it for real

```bash
./build/taiseia-bridge -c examples/config/appliance.conf
```

Every value in that file is a property of one deployment, which is why none of
them is compiled in. Set `serial_port`, `mqtt_host` and `device_id` at minimum;
the file documents the rest inline.

---

## Documentation

| | |
|---|---|
| [docs/architecture.md](docs/architecture.md) | layers, the two seams, why concurrency is not in the core |
| [docs/mqtt-api.md](docs/mqtt-api.md) | topic templates and payload schema |
| [docs/porting.md](docs/porting.md) | adding a platform, a protocol, or a concurrency model |
| [docs/testing.md](docs/testing.md) | what is verified, and what is not |
| [docs/adr/](docs/adr) | the decisions, each with the alternatives it rejected |
| [AGENTS.md](AGENTS.md) | instructions for coding agents |
| [CONTRIBUTING.md](CONTRIBUTING.md) | style, tests, pull requests |
| [SECURITY.md](SECURITY.md) | reporting, and the accepted limitations |

---

## Licence

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Written from the published standard; no code from any other implementation is
incorporated. This project is not certified, endorsed, or verified by any
standards body, and makes no claim of conformance to CNS 16014 or CNS 16090.
