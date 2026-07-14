# optical-netconf-roadm-sim

A **simulated optical network element** (ROADM + coherent transponder) with the
management stack used on real carrier-grade optical platforms: a C/C++ hardware
abstraction layer, a model-driven management plane (YANG / NETCONF via
sysrepo + Netopeer2), and a Protocol Buffers telemetry stream.

> Status: **M1 complete** (device core + HAL + unit tests + CI).
> M2 (YANG/NETCONF management plane), M3 (protobuf/ZeroMQ telemetry), and
> M4 (ARM Linux packaging) are in progress — see the roadmap below.

## Why

Optical transport software lives at the intersection of hardware behavior
(wavelengths, power budgets, FEC limits) and network management protocols
(YANG models, NETCONF, telemetry). This project rebuilds that whole vertical
slice in miniature, honestly labeled as a simulation, to demonstrate the
engineering patterns end to end:

```
        NETCONF client (netopeer2-cli / any controller)
                        │ XML / NETCONF (M2)
        ┌───────────────▼────────────────┐
        │  Netopeer2 server + sysrepo    │   model-driven management plane
        │  (YANG datastore)              │
        └───────────────┬────────────────┘
                        │ C HAL API (include/onsim/hal.h)
        ┌───────────────▼────────────────┐
        │  C++ device core (this repo)   │   simulated hardware
        │  ROADM: cross-connects, power  │
        │  Transponder: OSNR → pre-FEC   │
        │  BER, alarms                   │
        └───────────────┬────────────────┘
                        │ protobuf telemetry (M3)
                 ZeroMQ pub/sub → subscriber CLI
```

## What is modeled (M1)

**ROADM (`onsim::RoadmDevice`)** — N line ports on the ITU-T G.694.1 50 GHz
C-band grid (96 channels):
- wavelength cross-connects with real ROADM semantics: a channel may appear at
  most once per output port (wavelength-collision rule) and once per input port
- output power = input power − insertion loss; ports below the LOS threshold
  raise a loss-of-signal alarm
- deterministic per-tick power drift (seeded PRNG) so runs are reproducible

**Coherent transponder (`onsim::Transponder`)** — 100G/200G/400G line rates,
QPSK/8QAM/16QAM modulation:
- required OSNR grows with modulation density and symbol rate
- pre-FEC BER computed from the OSNR margin; crossing the SD-FEC ceiling
  (2e-2) raises a BER-degrade alarm
- carrier-grade rule enforced: rate/modulation changes are rejected while the
  port is admin-up

**HAL (`include/onsim/hal.h`)** — a C API in the style of vendor hardware SDKs
(opaque handles, integer status codes) so the management plane never touches
C++ types. This is the seam where the NETCONF plugin (M2) attaches.

## Build and test

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CI (GitHub Actions) runs the unit tests plus `gcovr` line coverage (fail under
85%), `cppcheck` static analysis, and a `valgrind` leak check on every push.

## Roadmap

| Milestone | Content | Status |
|---|---|---|
| M1 | C++ device core, HAL, GoogleTest suite, CMake, CI (coverage/cppcheck/valgrind) | ✅ |
| M2 | YANG module for the device; sysrepo + Netopeer2 NETCONF server; C++ plugin bridging datastore ↔ HAL | 🔄 |
| M3 | Protocol Buffers telemetry schema; ZeroMQ pub/sub publisher + Python subscriber | ⬜ |
| M4 | ARM Linux (Docker/QEMU) packaging, integration demo, debugging notes | ⬜ |

## Honest scope

This is a simulation built for learning and demonstration. It does not talk to
real optical hardware, and the physics (insertion loss, OSNR/BER curves) are
simplified textbook shapes chosen for plausible behavior, not calibrated
device models.
