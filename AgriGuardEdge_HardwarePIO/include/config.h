/**
 * AgriGuard Edge — include/config.h
 * Pin map + field calibration. Single place to adapt to the bench.
 * (Pin map: docs/architechture.md §6.1. Gate pins GPIO7/8 extend the
 * table — the arch gates both sensors but names no gate GPIO.)
 */
#pragma once
#include <stdint.h>

// ---------- Identity / backend ----------
#define AG_DEVICE_ID   "esp32_01"
#define AG_BACKEND_URL "http://192.168.1.10:82/api/ingest"  // ← demo hotspot IP
#define AG_WIFI_SSID   "AgriGuardDemo"
#define AG_WIFI_PASS   "demo-pass"

// ---------- Pins ----------
#define PIN_SOIL_ADC   1   // ADC1_CH0, capacitive probe (gated)
#define PIN_BATT_ADC   2   // ADC1_CH1, 100k/100k divider (gated w/ probe)
#define PIN_DHT11      4   // 4.7k pull-up to 3V3, ≥2 s between reads
#define PIN_RELAY      5   // via module driver; flyback + 1000uF at pump
#define PIN_FLOW       6   // post-level-shift pulses → PCNT0
#define PIN_SOIL_PWR   7   // gate: probe VCC (5 mA from GPIO, settle 200 ms)
#define PIN_FLOW_PWR   8   // gate: 5 V boost EN for YF-S201 (~15 mA)
#define PIN_TFT_BL     9   // LEDC PWM backlight — NEVER tie to 3V3 (§7)
#define PIN_TFT_CS     10
#define PIN_TFT_MOSI   11
#define PIN_TFT_SCK    12
#define PIN_TFT_DC     13
#define PIN_TFT_RST    14
#define PIN_BUTTON     0   // boot button: short=cycle screen, hold=reference

// ---------- Hydraulics ----------
#define AG_PPL_DEFAULT   5888.0f  // overwritten by §5.7 volume calibration
#define AG_V_TARGET_ML   250.0f   // stop-on-volume: every cycle comparable
#define AG_PROBE_TIMEOUT_S 120    // pump ON without volume ⇒ BLOCKAGE
#define AG_T_OBSERVE_S   180      // track d(moist)/dt after pump OFF

// ---------- Soil calibration (ADC 12-bit; measure your probe!) ----------
#define AG_SOIL_DRY_ADC  2800     // probe in air
#define AG_SOIL_WET_ADC  1200     // probe in water
// V_wetted: mL of soil water per 1 % moisture in the demo pot (§5.6a)
#define AG_V_WETTED_ML   40.0f

// ---------- TX policy (§7.1: defended 96 %, adaptive → 99 %) ----------
#define AG_SUMMARY_EVERY_S   900  // 15 min → 96/day fixed
#define AG_ADAPTIVE_DIV       4   // send every 4th unless CUSUM active
#define AG_TX_RING_SLOTS    256   // PSRAM queue, drop-oldest on overflow
