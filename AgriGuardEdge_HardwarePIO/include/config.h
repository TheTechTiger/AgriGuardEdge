/**
 * AgriGuard Edge — include/config.h
 * Pin map + field calibration. Single place to adapt to the bench.
 *
 * FINAL BOM: ESP32-S3 N16R8 · relay module · 12 V pump · ZJ-S201 flow
 * meter · RESISTIVE soil probe · DHT11 · ST7735. No battery: node on
 * USB 5 V (demo) or 12 V→5 V buck (field); pump on a 12 V adapter.
 */
#pragma once
#include <stdint.h>

// ---------- Identity / backend ----------
#define AG_DEVICE_ID   "esp32_01"
#define AG_BACKEND_URL "http://192.168.1.10:82/api/ingest"  // ← demo hotspot IP
#define AG_WIFI_SSID   "AgriGuardDemo"
#define AG_WIFI_PASS   "demo-pass"

// ---------- Pins ----------
#define PIN_SOIL_ADC   1   // ADC1_CH0, resistive probe AO (gated)
#define PIN_SUPPLY_ADC 2   // ADC1_CH1, 12 V rail via 100k/27k (gated w/ probe)
#define PIN_DHT11      4   // 4.7k pull-up to 3V3, ≥2 s between reads
#define PIN_RELAY      5   // relay module IN (VERIFY clicks on 3.3 V logic!);
                           // COM/NO switch the 12 V+ pump line; flyback 1N4007
                           // across pump + 1000 µF/25 V bulk cap; COMMON GND!
#define PIN_FLOW       6   // post-level-shift pulses → PCNT0
#define PIN_SOIL_PWR   7   // gate: probe VCC from GPIO (200 ms/sample).
                           // Mandatory on resistive: DC electrolysis eats the
                           // electrodes in days if left powered. Expect slow
                           // upward drift over weeks — the conservation gate
                           // (§5.6b) catches it as SENSOR_DRIFT by design.
#define PIN_FLOW_PWR   8   // gate: ZJ-S201 5 V via MOSFET/2nd relay (~15 mA).
                           // Demo fallback: tie to 5 V rail, note the cost.
#define PIN_TFT_BL     9   // LEDC PWM backlight — NEVER tie to 3V3
#define PIN_TFT_CS     10
#define PIN_TFT_MOSI   11
#define PIN_TFT_SCK    12
#define PIN_TFT_DC     13
#define PIN_TFT_RST    14
#define PIN_BUTTON     0   // boot button: short=cycle screen, hold=reference

// ---------- Hydraulics (ZJ-S201: F = 7.5 × Q → 450 p/L nominal) ----------
#define AG_PPL_DEFAULT   450.0f   // overwritten by §5.7 volume calibration
#define AG_V_TARGET_ML   250.0f   // stop-on-volume: every cycle comparable
#define AG_PROBE_TIMEOUT_S 120    // pump ON without volume ⇒ BLOCKAGE
#define AG_T_OBSERVE_S   180      // track d(moist)/dt after pump OFF

// ---------- Soil calibration (resistive, 12-bit; measure YOUR probe!) ----------
// Resistive: dry ≈ rail, wet ≈ low. Defaults are starting points only —
// run probe-in-air / probe-in-water and set these before trusting %.
#define AG_SOIL_DRY_ADC  3400     // probe in air
#define AG_SOIL_WET_ADC  1200     // probe in water
// V_wetted: mL of soil water per 1 % moisture in the demo pot (§5.6a)
#define AG_V_WETTED_ML   40.0f

// ---------- Supply monitoring (12 V rail, 100k/27k → ×4.7037) ----------
#define AG_SUPPLY_LOW_V  10.5f    // adapter sag / brownout ⇒ POWER_LOW

// ---------- Display demo override ----------
#define AG_DEMO_DISPLAY_ON 1   // judges: backlight FULL always, never dim/sleep.
                               // Set 0 to restore §9.1 power discipline.

// ---------- TX policy (§7.1: defended 96 %, adaptive → 99 %) ----------
#define AG_SUMMARY_EVERY_S   900  // 15 min → 96/day fixed
#define AG_ADAPTIVE_DIV       4   // send every 4th unless CUSUM active
#define AG_TX_RING_SLOTS    256   // PSRAM queue, drop-oldest on overflow
