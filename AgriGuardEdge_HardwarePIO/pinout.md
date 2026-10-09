# Pinout & wiring — ESP32-S3 DevKitC-1 (N16R8) ↔ final BOM

FINAL BOM: ESP32-S3 N16R8 · relay module · **12 V pump** · **ZJ-S201**
(450 p/L) · **resistive soil probe** · DHT11 · ST7735. No battery: node on
USB 5 V (demo) or 12 V→5 V buck (field); pump on a 12 V wall adapter.

Source of truth for GPIO numbers is `include/config.h`. **Wire the flow
level shifter first (§6.2) — 5 V into GPIO6 kills the pin.**

## MCU connections

| ESP32-S3 | To | Notes |
|---|---|---|
| GPIO1 (ADC1_CH0) | Resistive probe `AO` | Dry ≈ rail, wet ≈ low. Calibrate air/water (`AG_SOIL_DRY/WET_ADC`) |
| GPIO2 (ADC1_CH1) | 12 V rail via **100 k / 27 k** | ×4.7037 in firmware; <10.5 V ⇒ `POWER_LOW`. Gated with probe |
| GPIO4 | DHT11 `DATA` | 4.7 k–10 k pull-up to 3V3; ≥2 s between reads |
| GPIO5 | Relay module `IN` | **Verify it clicks on 3.3 V logic first.** COM/NO switch the **12 V+** pump line; flyback 1N4007 across pump + 1000 µF/25 V bulk cap; **common GND or nothing works** |
| GPIO6 | Flow pulses (**post-shift**) | PCNT0, rising edge, max 225 Hz |
| GPIO7 | Probe `VCC` | Gate: 200 ms/sample. **Mandatory on resistive** — steady DC electrolyses electrodes in days; gating stretches to a season |
| GPIO8 | ZJ-S201 5 V gate | Via MOSFET/2nd relay (~10 mA). Demo fallback: tie to 5 V rail, note the cost |
| GPIO9 | TFT `LED` | **LEDC PWM backlight. Never tie to 3V3** |
| GPIO10 | TFT `CS` | FSPI @ 20 MHz (breadboard-safe) |
| GPIO11 | TFT `MOSI` | FSPI |
| GPIO12 | TFT `SCK` | FSPI |
| GPIO13 | TFT `DC` | |
| GPIO14 | TFT `RST` | |
| GPIO0 | BOOT button | Short = cycle screen · 3 s hold = mark reference probe |
| GPIO15 | sacrificial MISO | **Leave unconnected.** Arduino 3.x remaps `MISO=-1` onto GPIO13 (our DC) as INPUT; GPIO15 is sacrificed instead (see `platformio.ini` note 4) |
| 5V pin | ZJ-S201 `V+` (red), via GPIO8 gate | From USB 5 V (demo) or buck output |
| GND | Common ground everywhere | 12 V supply GND **tied** to ESP GND — single star point at the DevKit |

Deliberately unused: GPIO19/20 (USB), GPIO43/44 (UART0), GPIO0/3/45/46
(strapping), GPIO26–37 (octal PSRAM).

## Flow level shifting (§6.2) — mandatory

ZJ-S201 `OUT` (yellow) swings to **5 V**; S3 abs-max is 3.6 V.

Preferred — 74AHCT125 (translation + Schmitt hysteresis):

```text
ZJ-S201 OUT ──► 74AHCT125 A_in   (5 V-tolerant input)
                74AHCT125 A_out ──► ESP32 GPIO6
                74AHCT125 VCC = 5 V  (outputs 3.3 V CMOS)
```

Fallback — resistor divider + pull-up:

```text
ZJ-S201 OUT ── 10 kΩ ──┬──► ESP32 GPIO6
                       │
                      20 kΩ to GND      (5 V × 20/30 = 3.33 V)
+ 10 k pull-up to 3V3 on GPIO6 so the line never floats unpowered
```

Verify with a meter: pulse pin idles ≤3.4 V while blowing through the
sensor **before** the ESP is connected. Nominal constant 450 p/L
(`F = 7.5 × Q`) — **overwritten by the 500 mL volume calibration (§5.7).**

## Plumbing order

```text
12 V adapter ──► [12 V pump] ──► relay ──► [ZJ-S201] ──► solenoid ──► zone
                                                      ▲
                                             measured here ──► probe in this zone
```

Meter **downstream of the pump, before the zone** (default). Mount the
ZJ-S201 **vertical, arrow up** (G1/2 thread). Leak demos tap **after**
the meter; a kink *after* the meter reads as blockage, not a leak (§11).

## Power tree (§6.4) — no battery anywhere

```text
12 V wall adapter (≥ pump stall current)
 ├── 12 V+ ──► pump (via relay COM/NO) + 1000 µF/25 V + 1N4007 flyback
 └── (buck) ──► 5 V node rail   [demo: USB cable instead of buck]
5 V node rail ──┬── ESP32-S3 (USB 5 V pin)
                ├── ST7735 logic + backlight (PWM GPIO9)
                ├── resistive probe (GPIO7-gated — anti-corrosion)
                ├── 12 V sense divider (gated) + DHT11
                └── ZJ-S201 (GPIO8-gated)
```

> **Mains safety:** the relay switches 12 V+, **not mains**. Keep the
> adapter brick off the breadboard and tie all GNDs together — a floating
> pump ground makes the relay chatter and the flow reading garbage.

## Soil calibration (resistive — do this, defaults are guesses)

```text
1. Probe in air  → read ADC → AG_SOIL_DRY_ADC   (≈3400 typical)
2. Probe in water → read ADC → AG_SOIL_WET_ADC  (≈1200 typical)
3. Bone-dry air may pin near 4095 → reads SENSOR_DISCONNECTED:
   correct — the probe is out of the soil. Say so in the demo.
```

## Pre-power checklist

- [ ] Relay **clicks on 3.3 V** from GPIO5 (else drive via transistor)
- [ ] Pulse line ≤3.4 V on meter, ESP disconnected
- [ ] 12 V adapter sized ≥ pump stall; common GND verified with meter
- [ ] 500 mL hand-pump test counts pulses (§5.7) → write `ppl` to NVS
- [ ] Backlight full-on at boot (GPIO9 PWM alive)
- [ ] `pio run` SUCCESS, flash, Serial shows `{"boot":N,"pcnt":1}`
- [ ] 10-min baseline soak, then hold BOOT 3 s on a known-good probe
