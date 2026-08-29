# taiseia-bridge

A TaiSEIA 101 (CNS 16014) appliance link with an MQTT northbound, in portable C99.

The bridge takes the **HNA role** defined by the standard: it is the requester on
a point-to-point UART link to a smart appliance (SA), reads the appliance's
capabilities and operating state, accepts control commands, and publishes
everything to an MQTT broker.

```
        MQTT broker
             ▲
             │  configurable topic templates, JSON payloads
      ┌──────┴───────┐
      │ taiseia-bridge│   ← HNA role
      └──────┬───────┘
             │  UART 9600 8N1, request/response, half duplex
        [ appliance (SA) ]
```

It runs on a Linux box, a Windows PC with a USB-UART adapter, or an embedded
Linux target. No external libraries: standard C99 plus what the platform
already provides.

---

## Status

**Early development. Nothing here has been verified against a physical
appliance.** Verification is against a built-in simulator and against test
vectors derived from the standard. See [docs/testing.md](docs/testing.md).

---

## Scope, and what this project deliberately does not do

Payloads carry **raw TaiSEIA service identifiers and values**. Nothing here
translates service id `3` into a name, or value `1` into `"on"`.

That is not an omission. Capabilities — including each service's data type and
its supported range or bit mask — are **queried from the appliance at run time**
using the standard's registration function. Interpreting what a service *means*
requires the per-appliance service tables in CNS 16014 Annex A, which are
licensed material. Consumers apply their own copy.

The practical benefit: a new appliance type needs no firmware change here.

---

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

## Running against the simulator

No hardware required.

```bash
./build/taiseia-bridge --link sim --config examples/config/simulator.conf
```

---

## Licence

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

This project is not certified, endorsed, or verified by any standards body, and
makes no claim of conformance to CNS 16014 or CNS 16090.
