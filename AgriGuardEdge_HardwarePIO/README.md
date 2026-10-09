# AgriGuardEdge_HardwarePIO — ESP32-S3 firmware (PlatformIO)

Edge-native irrigation anomaly detection: the node builds a diurnal
baseline, runs EWMA-CUSUM + persistence, and interrogates the field with
controlled-volume probe cycles through a YF-S201 flow meter. Decisions,
not data. Full algorithm spec: `../docs/architechture.md`.

## Layout

| Path | What |
|---|---|
| `src/main.cpp` | Scheduler: gated sensing → core → probe controller → TX → UI; RTC-persisted core across 60 s deep sleep |
| `src/flow_pcnt.*` | YF-S201 capture: PCNT hardware unit, ISR-poll fallback, EMA rate + volume |
| `src/sensors.*` | Power-gated soil/battery (200 ms bursts), bit-bang DHT11, relay |
| `src/ui_tft.*` | LVGL 9, 4 screens (LIVE / CONSERVATION / PROBE / ALERTS), PWM backlight + SLPIN |
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

1. **Level-shift the flow sensor first** (5 V TTL → 3V3, divider or
   74AHCT125, arch §6.2). Direct connection kills GPIO6.
2. Hand-pump 500 mL through the meter, count pulses, set
   `pulses_per_litre` (NVS `ppl`, arch §5.7). Default 5888 is datasheet,
   not truth.
3. Mount the YF-S201 **vertical, arrow up**.
4. One 18650 **with protection/BMS**; 1000 µF + flyback at the pump;
   pump on USB + MCU on battery is an approved fallback.
5. Backlight on GPIO9 PWM — never tie LED to 3V3 (costs ~1700 mAh/day).
6. Soak a 10-min baseline, run a reference probe (hold BOOT 3 s), then
   demo scenarios (arch §11). Short BOOT press cycles screens.

## Honest limits (arch §14)

Sub-1 L/min leaks invisible · one meter = one zone · ±5 % flow accuracy ·
S3 PCNT can't count through deep sleep (see `../docs/ERRATA.md`) ·
pump dominates energy — duty cycle, not firmware, sets battery life.
