/**
 * sensors.cpp — power-gated acquisition.
 * Resistive probe + divider live behind PIN_SOIL_PWR: 200 ms burst per
 * sample. Gating is load-saving AND probe-saving: steady DC through wet
 * soil electrolyses resistive electrodes in days; 200 ms/sample stretches
 * that to a season. Residual slow drift is caught by the conservation
 * gate (§5.6b) as SENSOR_DRIFT — stated, not hidden.
 * DHT11 bit-banged (no lib), ≥2 s between reads, NaN on timeout.
 */
#include <Arduino.h>
#include "sensors.h"
#include "config.h"

// 12 V rail via 100k/27k: Vadc = Vrail × 27/127 ⇒ Vrail = adc × 4.7037
#define SUPPLY_DIV_RATIO 4.7037f

static float s_last_moist = 40.f;
static uint32_t s_last_dht_ms = 0;
static float s_last_t = NAN, s_last_rh = NAN;

void sensors_init(void) {
  pinMode(PIN_SOIL_PWR, OUTPUT);
  digitalWrite(PIN_SOIL_PWR, LOW);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_DHT11, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

float soil_read(int *adc_out) {
  digitalWrite(PIN_SOIL_PWR, HIGH);
  delay(200);  // probe + divider settle
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) { acc += analogRead(PIN_SOIL_ADC); delay(5); }
  digitalWrite(PIN_SOIL_PWR, LOW);
  int adc = acc / 8;
  if (adc_out) *adc_out = adc;
  // dry..wet maps to 0..100 %, clamped; inverted (wet = lower ADC)
  float m = 100.f * (float)(AG_SOIL_DRY_ADC - adc) /
            (float)(AG_SOIL_DRY_ADC - AG_SOIL_WET_ADC);
  if (m < 0.f) m = 0.f;
  if (m > 100.f) m = 100.f;
  s_last_moist = m;
  return m;
}

float batt_read(void) {
  // Supply-rail monitor (12 V adapter through 100k/27k). Assumes the soil
  // gate is (or was just) on; re-enable briefly if needed.
  digitalWrite(PIN_SOIL_PWR, HIGH);
  delay(10);
  int mv = analogReadMilliVolts(PIN_SUPPLY_ADC);
  digitalWrite(PIN_SOIL_PWR, LOW);
  return (mv / 1000.f) * SUPPLY_DIV_RATIO;
}

static bool dht_bit(bool *v, uint32_t timeout_us) {
  uint32_t t0 = micros();
  int start = digitalRead(PIN_DHT11);
  while (digitalRead(PIN_DHT11) == start) {
    if (micros() - t0 > timeout_us) return false;
  }
  t0 = micros();
  start = digitalRead(PIN_DHT11);
  while (digitalRead(PIN_DHT11) == start) {
    if (micros() - t0 > timeout_us) return false;
  }
  *v = (micros() - t0) > 40;  // 26 µs=0, 70 µs=1
  return true;
}

bool dht_read(float *temp_c, float *rh) {
  uint32_t now = millis();
  if (now - s_last_dht_ms < 2000 && !isnan(s_last_t)) {
    *temp_c = s_last_t; *rh = s_last_rh; return true;
  }
  s_last_dht_ms = now;
  noInterrupts();
  pinMode(PIN_DHT11, OUTPUT);
  digitalWrite(PIN_DHT11, LOW);
  delay(20);
  digitalWrite(PIN_DHT11, HIGH);
  delayMicroseconds(30);
  pinMode(PIN_DHT11, INPUT_PULLUP);
  bool ok = dht_bit(NULL, 100) && dht_bit(NULL, 100);  // response 80+80 µs
  uint8_t b[5] = {0};
  for (int i = 0; ok && i < 40; i++) {
    bool bit = false;
    ok = dht_bit(&bit, 150);
    if (bit) b[i / 8] |= (0x80 >> (i % 8));
  }
  interrupts();
  uint8_t sum = b[0] + b[1] + b[2] + b[3];
  if (!ok || sum != b[4]) { *temp_c = *rh = NAN; return false; }
  s_last_rh = b[0] + b[1] / 256.f;
  s_last_t = b[2] + b[3] / 256.f;
  *temp_c = s_last_t; *rh = s_last_rh;
  return true;
}

void pump_cmd(bool on) { digitalWrite(PIN_RELAY, on ? HIGH : LOW); }
