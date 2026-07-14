# optical-netconf-roadm-sim

A **simulated optical network element** (ROADM + coherent transponder) with the
management stack used on real carrier-grade optical platforms: a C/C++ hardware
abstraction layer, a model-driven management plane (YANG / NETCONF via
sysrepo + Netopeer2), and a Protocol Buffers telemetry stream.

> Status: **M3 complete** — the full vertical slice works end to end in an
> ARM Linux container: a real NETCONF client provisions the device through
> Netopeer2 + sysrepo, and a Protocol Buffers telemetry stream (ZeroMQ
> pub/sub) reports live port powers, OSNR, and pre-FEC BER once per tick.

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
| M2 | YANG module; sysrepo + Netopeer2 NETCONF server; `onsim-netconfd` reconciliation daemon; end-to-end ncclient demo in an ARM Linux container | ✅ |
| M3 | Protocol Buffers telemetry schema; ZeroMQ pub/sub publisher in the daemon tick loop + Python subscriber CLI | ✅ |
| M4 | Demo transcript + debugging notes in `docs/`; CI builds the image and smoke-runs the demo | ✅ |

## Try the NETCONF demo (Docker)

```bash
docker build -t onsim-ne -f docker/Dockerfile .
docker run --rm onsim-ne demo    # or: shell / serve
```

The demo provisions a 400G/16QAM transponder and two wavelength
cross-connects over real NETCONF, reads operational state (note output
power = input power minus the 6 dB insertion loss), then tries to provision
a colliding wavelength and shows the device rejecting the whole transaction:

```
[3] provoke a wavelength collision (ch40 to port 2 again)
    device rejected it, as it should: cross-connect 'clash':
    wavelength collision on output port
```

The demo ends with the telemetry stream (protobuf over ZeroMQ pub/sub,
topic `telemetry`, port 5556):

```
tick=9  p1:-60.00dBm p2:-16.00dBm p3:-16.00dBm p4:-60.00dBm  \
        xpdr[up 400G QAM16] osnr=30.14dB ber=7.31e-06
```

Design note: `onsim-netconfd` applies configuration by declarative
reconciliation. On every transaction (SR_EV_CHANGE) it reads the candidate
config and reconciles the HAL to it; a HAL rejection fails the transaction so
the datastore never diverges from hardware, and SR_EV_ABORT reconciles back.
Rate/modulation changes are sequenced through admin-down automatically, the
way real NE management planes do.

## Notes

- [docs/debugging-notes.md](docs/debugging-notes.md): real root-cause write-ups
  from building this (version-matrix pinning, ncclient namespaces, a test that
  was wrong instead of the code).
- [docs/demo-transcript.txt](docs/demo-transcript.txt): full captured output of
  `docker run --rm onsim-ne demo`.

## Honest scope

This is a simulation built for learning and demonstration. It does not talk to
real optical hardware, and the physics (insertion loss, OSNR/BER curves) are
simplified textbook shapes chosen for plausible behavior, not calibrated
device models.
