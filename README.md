# AgriGuard Edge — edge-native irrigation anomaly detection (ESP32-S3)

One capacitive soil probe + one YF-S201 flow meter on a 18650. The node
builds a self-calibrating diurnal baseline, watches for sustained
departures, and **interrogates the irrigation**: pump a known volume,
check mass conservation. Flow is the gravitational truth the probe
cannot argue with.

Spec: `docs/architechture.md`. Deviations found during build: `docs/ERRATA.md`.

## Layout

| Path | What |
|---|---|
| `firmware/common/anomaly_core.h` | THE detector, pure C++ — stages ①–⑥, no Arduino. Compiled for ESP32 **and** host from this file |
| `firmware/test/test_core.cpp` | Native g++ unit tests (31/31) |
| `AgriGuardEdge_HardwarePIO/` | PlatformIO firmware: scheduler, PCNT flow, gated sensors, LVGL UI, TX |
| `tools/anomaly_replay/` | Host replay: injections, metrics, K/H sweep, C++ parity check |
| `AgriGuardEdge_Website/` | Flask + SQLite(WAL) + dashboard (ingest, events, probes, energy, scenarios) |

## Verify (no hardware)

```sh
# detector unit tests
cd firmware/test && g++ -std=c++17 -Wall -Wextra -I../common test_core.cpp -o test_core && ./test_core
# replay: 12/12 injections, 0 FA/day
cd tools/anomaly_replay && make all
# firmware compiles for S3 (1.04 MB, fits 4 MB partition)
cd AgriGuardEdge_HardwarePIO && pio run
# website API (needs flask: pip install -r AgriGuardEdge_Website/requirements.txt)
cd AgriGuardEdge_Website && python3 app.py  # :82
```

Replay result: **12/12 PASS, 0 false alarms/day, ~5 µs/eval** (`K=1.0/H=10`
fit via sweep). Mirror and compiled header agree event-for-event
(`make parity`).

## Demo scenarios (§11)

Dashboard → Scenario launcher, or physically: baseline soak → 250 mL
probe → loosen fitting (pump OFF ⇒ `CONTINUOUS_LEAK`) → crack fitting +
probe (⇒ `LEAK_SEVERE`) → kink hose + probe (⇒ `BLOCKAGE_NO_FLOW`) →
unplug probe (⇒ `SENSOR_DISCONNECTED`) → pour water (⇒ `EXTERNAL_EVENT`,
no alert).
