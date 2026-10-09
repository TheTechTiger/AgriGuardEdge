/**
 * flow_pcnt.cpp — ZJ-S201 pulse capture (§5.1, §6.2). 450 p/L nominal
 * (F = 7.5 × Q), bench-calibrated per §5.7 — never trusted from vendor.
 * Primary: ESP32-S3 PCNT hardware unit (counts while CPU is awake, no ISR
 * jitter; max 225 Hz at 30 L/min). Degraded: GPIO interrupt poll
 * (§12.1 MoSCoW row). Rate is EMA-smoothed to kill quantisation noise,
 * which matters more at 450 p/L than at finer constants.
 */
#include <Arduino.h>
#include "flow_pcnt.h"
#include "config.h"

#if __has_include("driver/pulse_cnt.h")
#include "driver/pulse_cnt.h"
#define FLOW_HAVE_PCNT_NEW 1
#elif __has_include("driver/pcnt.h")
#include "driver/pcnt.h"
#define FLOW_HAVE_PCNT_LEGACY 1
#endif
#if !defined(FLOW_HAVE_PCNT_NEW) && !defined(FLOW_HAVE_PCNT_LEGACY)
#define FLOW_HAVE_PCNT 0
#endif

#define FLOW_ALPHA_CH 0.25f

static float s_ppl = AG_PPL_DEFAULT;
static float s_rate = 0.f, s_vol_ml = 0.f;
static uint32_t s_last = 0, s_total = 0;

#if defined(FLOW_HAVE_PCNT_NEW)
static pcnt_unit_handle_t s_unit = NULL;
static int s_pcnt = 1;
static volatile uint32_t s_isr = 0;  // poll fallback if init fails
static void IRAM_ATTR flow_isr() { s_isr++; }
#elif defined(FLOW_HAVE_PCNT_LEGACY)
static int s_pcnt = 1;  // legacy driver: unit 0, channel 0, int16 counter
static volatile uint32_t s_isr = 0;  // poll fallback if legacy init fails
static void IRAM_ATTR flow_isr() { s_isr++; }
#else
static volatile uint32_t s_isr = 0;
static void IRAM_ATTR flow_isr() { s_isr++; }
static int s_pcnt = 0;
#endif

void flow_set_ppl(float ppl) { if (ppl > 100.f) s_ppl = ppl; }

bool flow_using_pcnt(void) { return s_pcnt == 1; }

void flow_power(bool on) {
  pinMode(PIN_FLOW_PWR, OUTPUT);
  digitalWrite(PIN_FLOW_PWR, on ? HIGH : LOW);
  if (on) delay(50);  // Hall sensor settle
}

void flow_init(float ppl) {
  flow_set_ppl(ppl);
  pinMode(PIN_FLOW, INPUT);
  flow_power(false);
#if defined(FLOW_HAVE_PCNT_NEW)
  pcnt_unit_config_t u = {};
  u.high_limit = 30000;
  u.low_limit = -1;
  if (pcnt_new_unit(&u, &s_unit) != ESP_OK) { s_unit = NULL; s_pcnt = 0; }
  else {
    pcnt_chan_config_t ch = {};
    ch.edge_gpio_num = (gpio_num_t)PIN_FLOW;
    ch.level_gpio_num = GPIO_NUM_NC;
    pcnt_channel_handle_t chan = NULL;
    if (pcnt_new_channel(s_unit, &ch, &chan) != ESP_OK) { s_pcnt = 0; }
    else {
      pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_HOLD);
      pcnt_unit_enable(s_unit);
      pcnt_unit_start(s_unit);
      s_pcnt = 1;
    }
  }
  if (!s_pcnt) {  // fall through to poll mode
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flow_isr, RISING);
  }
#elif defined(FLOW_HAVE_PCNT_LEGACY)
  // Legacy driver (this Arduino package ships no pulse_cnt.h): same PCNT
  // peripheral, int16 hardware counter, glitch filter for breadboard noise.
  pcnt_config_t pc = {};
  pc.pulse_gpio_num = PIN_FLOW;
  pc.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  pc.channel = PCNT_CHANNEL_0;
  pc.unit = PCNT_UNIT_0;
  pc.pos_mode = PCNT_COUNT_INC;
  pc.neg_mode = PCNT_COUNT_DIS;
  pc.lctrl_mode = PCNT_MODE_KEEP;
  pc.hctrl_mode = PCNT_MODE_KEEP;
  pc.counter_h_lim = 30000;
  pc.counter_l_lim = -1;
  if (pcnt_unit_config(&pc) != ESP_OK) {
    s_pcnt = 0;
  } else {
    pcnt_set_filter_value(PCNT_UNIT_0, 100);  // ~1.25 µs glitch blanking
    pcnt_filter_enable(PCNT_UNIT_0);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    s_pcnt = 1;
  }
  if (!s_pcnt) {
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flow_isr, RISING);
  }
#else
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flow_isr, RISING);
#endif
}

static uint32_t flow_hw_count(void) {
#if defined(FLOW_HAVE_PCNT_NEW)
  if (s_unit) {
    int v = 0;
    if (pcnt_unit_get_count(s_unit, &v) == ESP_OK) return (uint32_t)v;
  }
  return s_isr;
#elif defined(FLOW_HAVE_PCNT_LEGACY)
  int16_t v = 0;
  if (pcnt_get_counter_value(PCNT_UNIT_0, &v) == ESP_OK)
    return (uint32_t)(uint16_t)v;  // wrap-safe vs s_last
  return 0;
#else
  uint32_t v;
  noInterrupts();
  v = s_isr;
  interrupts();
  return v;
#endif
}

void flow_reset_volume(void) {
  s_vol_ml = 0.f;
  s_last = flow_hw_count();
}

float flow_poll(float dt_s) {
  uint32_t now = flow_hw_count();
  uint32_t d = now - s_last;  // wrap-safe
  s_last = now;
  s_total += d;
  float litres = (float)d / s_ppl;
  float raw_lpm = (dt_s > 0.f) ? (litres * 60.0f / dt_s) : 0.f;
  s_rate += FLOW_ALPHA_CH * (raw_lpm - s_rate);
  s_vol_ml += litres * 1000.0f;
  return s_rate;
}

float flow_volume_ml(void) { return s_vol_ml; }
uint32_t flow_pulses_total(void) { return s_total; }
