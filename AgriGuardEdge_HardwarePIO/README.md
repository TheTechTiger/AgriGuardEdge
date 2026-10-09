# AgriGuardEdge_HardwarePIO — ESP32-S3 firmware (PlatformIO)

Edge-native irrigation anomaly detection: the node builds a diurnal
baseline, runs EWMA-CUSUM + persistence, and interrogates the field with
controlled-volume probe cycles through a ZJ-S201 flow meter. Decisions,
not data. Full algorithm spec: `../docs/architechture.md`.

FINAL BOM: ESP32-S3 N16R8 · relay module · 12 V pump (wall adapter) ·
ZJ-S201 (450 p/L) · resistive soil probe · DHT11 · ST7735. Mains-powered
demo (12 V + USB); no battery — §7's battery table is the off-grid
design reference.

## Layout

| Path | What |
|---|---|
| `src/main.cpp` | Scheduler: gated sensing → core → probe controller → TX → UI; RTC-persisted core across 60 s deep sleep |
| `src/flow_pcnt.*` | ZJ-S201 capture: PCNT hardware unit, ISR-poll fallback, EMA rate + volume |
| `src/sensors.*` | Power-gated soil/battery (200 ms bursts), bit-bang DHT11, relay |
| `src/ui_tft.*` | LVGL 9, 4 screens (LIVE / CONSERVATION / PROBE / ALERTS), backlight always on |
| `src/net_tx.*` | WiFi connect→send→disconnect, PSRAM 256-slot ring, idempotent `msg_id`, backoff |
| `include/config.h` | Pins, calibration, TX policy — **edit this for your bench** |
| `include/lv_conf.h` | LVGL config (200 ms refresh, no animations) |
| `firmware/common/anomaly_core.h` | THE detector, pure C++ — shared with the host harness, single source of truth |
| `firmware/test/test_core.cpp` | Native unit tests (no Arduino needed) |
| `tools/anomaly_replay/` | Host replay: fault injections, metrics, K/H sweep, C++ parity |
| `partitions_custom.csv` | 16 MB layout: 2×4 MB app + LittleFS |
| `platformio.ini` | S3 N16R8 + ST7735 + TFT_eSPI + LVGL 9 (see §6.6 deviations) |

## Build / test (no hardware)

```sh
pio run                                   # S3 firmware (expect SUCCESS, ~1 MB)
cd firmware/test && g++ -std=c++17 -Wall -Wextra -I../common test_core.cpp -o test_core && ./test_core
cd ../../tools/anomaly_replay && make all # 12/12 injections, parity, metrics
```

## Bring-up order (do not skip)

1. **Level-shift + 500 mL calibration first** (saves the GPIO; overwrites
   the 450 p/L nominal — never trust the vendor number).
2. Hand-pump 500 mL through the meter, count pulses, set
   `pulses_per_litre` (NVS `ppl`, arch §5.7). **Verify the relay clicks on
   3.3 V logic and all GNDs are common before first power-up.**
3. Mount the ZJ-S201 **vertical, arrow up**.
4. 12 V adapter sized ≥ pump stall current; node on USB (demo) or buck.
5. Backlight on GPIO9 PWM — never tie LED to 3V3.
6. Soak a 10-min baseline, run a reference probe (hold BOOT 3 s), then
   demo scenarios (arch §11). Short BOOT press cycles screens.

## Honest limits (arch §14)

Sub-1 L/min leaks invisible · one meter = one zone · unquoted flow
accuracy (calibration IS the datasheet) · resistive probe corrodes
(gating + conservation gate are the answer) · S3 PCNT can't count
through deep sleep (see `../docs/ERRATA.md`) · mains-powered demo —
energy claim is measured supply current per policy, not days-per-charge.
