# Pinout & wiring — ESP32-S3 DevKitC-1 (N16R8) ↔ field hardware

Source of truth for GPIO numbers is `include/config.h`; this file is the
bench-readable version. Pin map: arch §6.1. **Wire the flow level shifter
first (§6.2) — 5 V into GPIO6 kills the pin.**

## MCU connections

| ESP32-S3 | To | Notes |
|---|---|---|
| GPIO1 (ADC1_CH0) | Soil probe `AOUT` | Capacitive analog, gated (§6.4) |
| GPIO2 (ADC1_CH1) | Battery divider midpoint | 100 k / 100 k, gated with probe |
| GPIO4 | DHT11 `DATA` | 4.7 k–10 k pull-up to 3V3; ≥2 s between reads |
| GPIO5 | Relay module `IN` | Via module transistor; **flyback diode across pump + 1000 µF bulk cap** |
| GPIO6 | Flow pulses (**post-shift**) | PCNT0, rising edge. `GPIO6` is ADC2_CH0 — irrelevant for digital in |
| GPIO7 | Soil probe `VCC` | Gate: probe powered 200 ms/sample only (~5 mA from GPIO) |
| GPIO8 | 5 V boost `EN` | Gate: YF-S201 powered during pump-on + leak checks only (~15 mA) |
| GPIO9 | TFT `LED` | **LEDC PWM backlight. Never tie to 3V3 (~1700 mAh/day, §7)** |
| GPIO10 | TFT `CS` | FSPI |
| GPIO11 | TFT `MOSI` | FSPI @ 20 MHz (breadboard-safe; 40 MHz artefacts, §6.6) |
| GPIO12 | TFT `SCK` | FSPI |
| GPIO13 | TFT `DC` | |
| GPIO14 | TFT `RST` | |
| GPIO0 | BOOT button | Short = cycle screen · 3 s hold = mark reference probe |
| 3V3 | TFT `VCC`, probe? No — probe VCC is GPIO7 | Soil divider top also gated |
| GND | Common ground everywhere | Single star point at the DevKit |
| 5V (boost out) | YF-S201 `V+` (red), pump rail via relay | Shares pump boost rail |

Deliberately unused: GPIO19/20 (USB), GPIO43/44 (UART0), GPIO0/3/45/46
(strapping), GPIO26–37 (octal PSRAM).

## Flow level shifting (§6.2) — mandatory

YF-S201 `OUT` (yellow) swings to **5 V**; S3 abs-max is 3.6 V.

Preferred — 74AHCT125 (translation + Schmitt hysteresis):

```text
YF-S201 OUT ──► 74AHCT125 A_in   (5 V-tolerant input)
                74AHCT125 A_out ──► ESP32 GPIO6
                74AHCT125 VCC = 5 V  (outputs 3.3 V CMOS)
```

Fallback — resistor divider + pull-up:

```text
YF-S201 OUT ── 10 kΩ ──┬──► ESP32 GPIO6
                       │
                      20 kΩ to GND      (5 V × 20/30 = 3.33 V)
+ 10 k pull-up to 3V3 on GPIO6 so the line never floats unpowered
```

Verify with a meter: pulse pin idles ≤3.4 V while blowing through the
sensor **before** the ESP is connected.

## Plumbing order

```text
battery ──► [mini pump] ──► relay ──► [YF-S201] ──► solenoid ──► zone
                                              ▲
                                     measured here ──► probe in this zone
```

Meter **downstream of the pump, before the zone** (default). Mount the
YF-S201 **vertical, arrow up**. Leak demos tap **after** the meter; a kink
*after* the meter reads as blockage, not a leak (arch §11).

## Power tree (§6.4)

```text
18650 (PROTECTED cell + BMS — bare cell + pump = fire risk)
 ├── 3.3 V boost ──┬── ESP32-S3
 │                 ├── ST7735 logic + backlight (PWM GPIO9)
 │                 ├── soil probe (GPIO7-gated) + divider
 │                 └── DHT11
 └── 5 V boost ──┬── YF-S201 (GPIO8-gated, ~15 mA → gate or it eats 360 mAh/day)
                 └── pump (via relay)
```

Fallback: pump on USB, MCU on battery — all algorithmic claims hold.

## Pre-power checklist

- [ ] Pulse line ≤3.4 V on meter, ESP disconnected
- [ ] 500 mL hand-pump test counts pulses (§5.7) → write `ppl` to NVS
- [ ] Backlight dims (15 % ↔ 100 % sweep at boot proves GPIO9 PWM)
- [ ] `pio run` SUCCESS, flash, Serial shows `{"boot":N,"pcnt":1}`
- [ ] 10-min baseline soak, then hold BOOT 3 s on a known-good probe
