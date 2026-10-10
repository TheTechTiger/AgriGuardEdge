/**
 * AgriGuard Edge — src/main.cpp
 * Scheduler: gated sensing → anomaly_core → probe controller → TX → UI.
 * NORMAL sleeps deep 60 s with the core in RTC RAM; SUSPECT/ALARM and
 * probe cycles stay awake at 10/5 s. Decisions, not data (§2).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_sleep.h"
#include "anomaly_core.h"
#include "config.h"
#include "flow_pcnt.h"
#include "sensors.h"
#include "ui_tft.h"
#include "net_tx.h"
#include "ws_link.h"
#include "server_cfg.h"

// ---------- RTC-persisted detector (deep-sleep safe, ~600 B) ----------
RTC_DATA_ATTR static ag_core_t rtc_core;
RTC_DATA_ATTR static uint32_t rtc_total_sleep_s = 0;
RTC_DATA_ATTR static uint32_t rtc_msg_ctr = 0;
RTC_DATA_ATTR static uint8_t rtc_valid = 0;
RTC_DATA_ATTR static uint8_t rtc_probing = 0;   // observe phase active
RTC_DATA_ATTR static uint32_t rtc_probe_t0 = 0; // observe start (uptime s)
RTC_DATA_ATTR static float rtc_probe_m0 = 0;    // moisture at pump-off
RTC_DATA_ATTR static uint8_t rtc_probe_n = 0;

static Preferences prefs;
static ag_config_t cfg;
static uint32_t boot_count = 0;
static char alert_ring[3][96];
// Summary scheduling must survive deep sleep → RTC (uptime seconds).
RTC_DATA_ATTR static uint32_t rtc_last_summary_up = 0;
RTC_DATA_ATTR static uint32_t rtc_last_probe_up = 0;
RTC_DATA_ATTR static int rtc_summary_div = 0;
RTC_DATA_ATTR static uint32_t rtc_lk_total0 = 0;  // micro-leak window start
RTC_DATA_ATTR static uint32_t rtc_lk_t0 = 0;
RTC_DATA_ATTR static uint8_t rtc_lk_fired = 0;
// Summary window accumulators (RTC: a 15-min window spans deep sleeps).
RTC_DATA_ATTR static float rtc_wsum = 0, rtc_wsum2 = 0;
RTC_DATA_ATTR static float rtc_wmin = 1e9f, rtc_wmax = -1e9f;
RTC_DATA_ATTR static float rtc_wt = 0, rtc_wrh = 0;
RTC_DATA_ATTR static uint32_t rtc_wn = 0, rtc_evals = 0, rtc_wdht = 0;
static uint32_t last_interact_ms = 0;


static uint32_t uptime_s(void) { return rtc_total_sleep_s + millis() / 1000; }
static uint8_t hour_now(void) { return (uptime_s() / 3600) % 24; }

static void msg_id(char *out, size_t n) {
  snprintf(out, n, "%s:boot%u:%u", AG_DEVICE_ID, boot_count, rtc_msg_ctr++);
}

static void tx_json(const char *kind, const char *event, const char *sev,
                    ag_result_t *r, float vbat, float flow_lpm, uint8_t pump) {
  char id[48];
  msg_id(id, sizeof(id));
  JsonDocument d;
  d["msg_id"] = id;
  d["device_id"] = AG_DEVICE_ID;
  d["boot_count"] = boot_count;
  d["uptime_ms"] = millis() + rtc_total_sleep_s * 1000UL;
  d["kind"] = kind;
  if (event) d["event"] = event;
  if (sev) d["severity"] = sev;
  JsonObject e = d["evidence"].to<JsonObject>();
  if (r) {
    e["z_soil"] = r->z_soil;
    e["cusum_up"] = r->cusum_up;
    e["cusum_down"] = r->cusum_down;
    e["baseline_moist"] = r->baseline_moist;
    e["sigma"] = r->sigma;
    e["deficit"] = r->deficit;
    e["V_delivered_ml"] = r->v_delivered_ml;
    e["V_soil_delta_ml"] = r->v_soil_delta_ml;
    e["V_et_ml"] = r->v_et_ml;
    e["residual_ratio"] = r->residual_ratio;
    e["ratio_vs_reference"] = r->ratio_vs_ref;
    e["flow_rate_lpm"] = flow_lpm;
    e["pump_state"] = pump ? "ON" : "OFF";
    e["flow_calibrated_ppl"] = rtc_core.pulses_per_litre;
    e["state"] = r->state == 2 ? "ALARM" : (r->state == 1 ? "SUSPECT" : "NORMAL");
    e["cadence_s"] = ag_cadence_s(r->state, rtc_probing);
  }
  if (!strcmp(kind, "summary") && rtc_wn > 0) {
    // Real window stats (§8), not zeros: accumulated across deep sleeps.
    float avg = rtc_wsum / rtc_wn;
    float var = rtc_wsum2 / rtc_wn - avg * avg;
    JsonObject w = d["window"].to<JsonObject>();
    w["avg_moisture"] = avg;
    w["min"] = rtc_wmin; w["max"] = rtc_wmax;
    w["std"] = var > 0.f ? sqrtf(var) : 0.f;
    w["avg_temp"] = rtc_wdht ? rtc_wt / rtc_wdht : 0; w["avg_rh"] = rtc_wdht ? rtc_wrh / rtc_wdht : 0;
    JsonObject en = d["energy"].to<JsonObject>();
    en["vbat"] = vbat;               // 12 V rail volts (not a cell)
    en["soc_pct"] = 100;             // mains-powered: SoC meaningless
    en["mah_today"] = 0;             // no current sensor (§1.3) — stated
    JsonObject sv = d["savings"].to<JsonObject>();
    sv["readings_generated"] = rtc_evals;
    sv["messages_sent"] = tx_sent_total();
    sv["suppressed"] = tx_suppressed_total();
    rtc_wsum = rtc_wsum2 = 0; rtc_wmin = 1e9f; rtc_wmax = -1e9f;
    rtc_wt = rtc_wrh = 0; rtc_wn = 0; rtc_wdht = 0;
  }
  char buf[768];
  serializeJson(d, buf, sizeof(buf));
  tx_queue(kind, buf);
}

static void note_alert(const char *event, const char *sev) {
  snprintf(alert_ring[2], sizeof(alert_ring[2]), "%s", alert_ring[1]);
  snprintf(alert_ring[1], sizeof(alert_ring[1]), "%s", alert_ring[0]);
  snprintf(alert_ring[0], sizeof(alert_ring[0]), "[%s] %s", sev, event);
}

static bool button_held(uint32_t ms) {
  if (digitalRead(PIN_BUTTON) == HIGH) return false;
  uint32_t t0 = millis();
  while (digitalRead(PIN_BUTTON) == LOW && millis() - t0 < ms) delay(10);
  return millis() - t0 >= ms;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  server_cfg_init();  // NVS server IP first: tx + ws use it below
  prefs.begin("agriguard", false);
  boot_count = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", boot_count);
  float ppl = prefs.getFloat("ppl", AG_PPL_DEFAULT);
  float rref = prefs.getFloat("rref", 0.f);

  if (!rtc_valid || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    ag_init(&rtc_core);
    rtc_valid = 1;
    rtc_total_sleep_s = 0;
  }
  rtc_core.pulses_per_litre = ppl;
  if (rref > 0.001f) rtc_core.r_ref = rref;
  cfg = ag_config_default();
  cfg.v_wetted_ml = AG_V_WETTED_ML;
  cfg.batt_low_v = AG_SUPPLY_LOW_V;  // 12 V rail monitor, not a cell

  sensors_init();
  flow_init(ppl);
  flow_reset_volume();  // re-baseline after any sleep reset
  tx_init();
  ws_init();  // realtime pump-test link (non-blocking; reconnects itself)
  ui_init();
  // Splash only on real boot — deep-sleep timer wakes go straight to LIVE
  // so the 60 s cadence isn't eaten by animation.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER)
    ui_boot_splash(boot_count, flow_using_pcnt() ? 1 : 0);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  memset(alert_ring, 0, sizeof(alert_ring));
  snprintf(alert_ring[0], sizeof(alert_ring[0]), "boot %u", boot_count);
  Serial.printf("{\"boot\":%u,\"pcnt\":%d}\n", boot_count, flow_using_pcnt());
  server_cfg_print();
  Serial.println(F("type 'help' for serial commands (set ip a.b.c.d)"));
}

// One controlled-volume probe: pump to V_target, observe, conserve.
static void probe_cycle(bool mark_reference) {
  rtc_probing = 1;
  Serial.println("{\"probe\":\"start\"}");
  flow_power(true);
  pump_cmd(true);
  flow_reset_volume();
  ui_show(2);
  uint32_t t0 = millis();
  float rate = 0.f;
  while (flow_volume_ml() < AG_V_TARGET_ML) {
    rate = flow_poll(1.f);
    ui_snapshot_t s = {};
    s.pump_on = 1; s.flow_lpm = rate;
    s.vol_ml = flow_volume_ml(); s.vol_target = AG_V_TARGET_ML;
    s.pulses = flow_pulses_total();
    ui_update(&s);
    ui_power_tick(1, 1, millis());
    if (millis() - t0 > AG_PROBE_TIMEOUT_S * 1000UL) {
      pump_cmd(false); flow_power(false);
      ag_result_t r = {};
      tx_json("alert", "BLOCKAGE_NO_FLOW", "HIGH", &r, batt_read(), 0, 1);
      note_alert("BLOCKAGE_NO_FLOW", "HIGH");
      tx_flush();
      rtc_last_probe_up = uptime_s(); rtc_probing = 0;
      return;  // pump ON, volume never arrived (§5.7)
    }
    delay(1000);
  }
  pump_cmd(false);
  float v_del = flow_volume_ml();
  flow_power(false);
  int adc0 = 0;
  float m0 = soil_read(&adc0);
  // Observe 180 s at 5 s cadence, tracking d(moist)/dt
  float m1 = m0;
  for (int t = 0; t < AG_T_OBSERVE_S; t += 5) {
    delay(5000);
    int a = 0;
    m1 = soil_read(&a);
    ui_snapshot_t s = {};
    s.moisture = m1; s.dsoil_dt = (m1 - m0) / ((t + 5) / 60.f);
    ui_update(&s);
  }
  float v_et = rtc_core.et_rate[hour_now()] * (AG_T_OBSERVE_S / 3600.f) *
               AG_V_WETTED_ML;
  ag_result_t r = {};
  ag_event_t v = ag_probe_evaluate(&rtc_core, &cfg, v_del, m1 - m0, v_et,
                                   mark_reference ? 1 : 0, &r);
  if (mark_reference) {
    prefs.putFloat("rref", rtc_core.r_ref);
    Serial.printf("{\"rref\":%.3f}\n", rtc_core.r_ref);
  }
  rtc_probe_n++;
  tx_json("probe_result", ag_event_name(v),
          v == AG_EV_IRRIGATION_OK ? "INFO" : "HIGH", &r, batt_read(), 0, 0);
  note_alert(ag_event_name(v), v == AG_EV_IRRIGATION_OK ? "INFO" : "HIGH");
  tx_flush();
  ui_snapshot_t s = {};
  s.v_del = v_del; s.v_soil = r.v_soil_delta_ml; s.v_et = v_et;
  s.v_resid = v_del - r.v_soil_delta_ml - v_et;
  s.ratio = r.residual_ratio; s.vs_ref = r.ratio_vs_ref;
  s.event = ag_event_name(v); s.probe_n = rtc_probe_n;
  ui_update(&s);
  ui_show(1);
  delay(8000);
  rtc_last_probe_up = uptime_s(); rtc_probing = 0;
}

static void tx_micro_leak(uint32_t pulses, uint32_t window_s, float rate) {
  // Firmware-level verdict (long-window integration lives here, not in the
  // per-eval core): sub-floor leak while the pump was OFF the whole window.
  char id[48];
  msg_id(id, sizeof(id));
  JsonDocument d;
  d["msg_id"] = id;
  d["device_id"] = AG_DEVICE_ID;
  d["boot_count"] = boot_count;
  d["uptime_ms"] = millis() + rtc_total_sleep_s * 1000UL;
  d["kind"] = "alert";
  d["event"] = "MICRO_LEAK";
  d["severity"] = "MED";
  JsonObject e = d["evidence"].to<JsonObject>();
  e["off_pulses"] = pulses;
  e["window_s"] = window_s;
  e["impl_max_lpm"] = 60.0f * pulses / AG_PPL_DEFAULT / window_s;
  e["state"] = rtc_core.state == 2 ? "ALARM" : (rtc_core.state == 1 ? "SUSPECT" : "NORMAL");
  e["flow_rate_lpm"] = rate;
  e["pump_state"] = "OFF";
  e["reason"] = "pulses with pump OFF all window: below meter floor, above zero";
  char buf[384];
  serializeJson(d, buf, sizeof(buf));
  if (tx_queue("alert", buf)) tx_flush();
}

void loop() {
  static uint32_t eval_ms = 0;
  static int8_t prev_man = -1;
  server_cfg_poll_serial();  // Serial "set ip" — every pass, even between evals
  ws_tick(uptime_s());  // realtime link first: needs every pass, not just evals
#if AG_DEMO_LIVE_1S
  uint32_t cadence = 1;  // demo: fresh sensor values on screen every second
#else
  uint32_t cadence = ag_cadence_s(rtc_core.state, rtc_probing);
#endif
  if (ws_manual() >= 0) cadence = 1;  // manual pump test streams realtime
  if (millis() - eval_ms < cadence * 1000UL && eval_ms) {
    // button: short press cycles screens
    if (digitalRead(PIN_BUTTON) == LOW) {
      delay(50);
      if (digitalRead(PIN_BUTTON) == LOW) {
        last_interact_ms = millis();
        if (button_held(3000)) {
          probe_cycle(true);  // hold = mark reference (§5.6a)
          last_interact_ms = millis();
        } else {
          ui_show(ui_current() + 1);
          while (digitalRead(PIN_BUTTON) == LOW) delay(10);
        }
      }
    }
    delay(50);
    return;
  }
  eval_ms = millis();

  // ---- sense (all gated) ----
  int adc = 0;
  float moist = soil_read(&adc);
  float vbat = batt_read();
  float t, rh;
  bool dht_ok = dht_read(&t, &rh);
  flow_power(true);
  float rate = flow_poll((float)cadence);
  // Manual override (websocket pump test) wins over everything except an
  // active probe cycle. Rising edge zeroes the test volume.
  int8_t man = ws_manual();
  if (man >= 0) {
    if (prev_man < 0) flow_reset_volume();
    pump_cmd(man ? true : false);
    flow_power(true);
  }
  prev_man = man;
  bool pump_on = digitalRead(PIN_RELAY);
  if (!rtc_probing && !pump_on) flow_power(false);  // gate when idle

  // ---- micro-leak watch: integrate pump-OFF pulses over a long window ----
  // Rate-based CONTINUOUS_LEAK cannot see below ~1 L/min; a slow weep still
  // twitches the turbine. Any pump ON (or probe) restarts the window.
  uint32_t lk_pulses = 0;
  if (pump_on || rtc_probing) {
    rtc_lk_total0 = flow_pulses_total();
    rtc_lk_t0 = uptime_s();
    rtc_lk_fired = 0;
  } else {
    if (rtc_lk_t0 == 0) {
      rtc_lk_total0 = flow_pulses_total();
      rtc_lk_t0 = uptime_s();
    }
    lk_pulses = flow_pulses_total() - rtc_lk_total0;  // wrap-safe
    uint32_t lk_dt = uptime_s() - rtc_lk_t0;
    if (!rtc_lk_fired && lk_dt >= AG_MICRO_LEAK_WINDOW_S &&
        lk_pulses >= AG_MICRO_LEAK_PULSES) {
      rtc_lk_fired = 1;
      note_alert("MICRO_LEAK", "MED");
      tx_micro_leak(lk_pulses, lk_dt, rate);
    }
  }

  // ---- window accumulators (feed the next real summary) ----
  rtc_wsum += moist; rtc_wsum2 += moist * moist; rtc_wn++; rtc_evals++;
  if (moist < rtc_wmin) rtc_wmin = moist;
  if (moist > rtc_wmax) rtc_wmax = moist;
  if (dht_ok) { rtc_wt += t; rtc_wrh += rh; rtc_wdht++; }

  // ---- ① health gate, then ②③④⑥ ----
  ag_sample_t sm = {};
  sm.moisture_pct = moist; sm.adc_raw = adc;
  sm.temp_c = dht_ok ? t : NAN; sm.rh_pct = dht_ok ? rh : NAN;
  sm.flow_lpm = rate; sm.vbat_v = vbat;
  sm.pump_on = pump_on ? 1 : 0;
  sm.t_sec = uptime_s(); sm.hour = hour_now();

  ag_event_t fault = ag_health_gate(&rtc_core, &cfg, &sm, NULL);
  ag_result_t r = {};
  const char *ev_name = NULL;
  if (fault == AG_EV_NONE || fault == AG_EV_SENSOR_TIMEOUT) {
    r = ag_step(&rtc_core, &cfg, &sm, (float)cadence);
    if (fault == AG_EV_SENSOR_TIMEOUT) {
      tx_json("alert", "SENSOR_TIMEOUT", "MED", &r, vbat, rate, pump_on);
      note_alert("SENSOR_TIMEOUT", "MED");
    } else if (r.event != AG_EV_NONE && r.severity != AG_SEV_INFO) {
      ev_name = ag_event_name(r.event);
      tx_json("alert", ev_name, ag_sev_name(r.severity), &r, vbat, rate, pump_on);
      note_alert(ev_name, ag_sev_name(r.severity));
      tx_flush();  // alerts send immediately
    } else if (r.event == AG_EV_EXTERNAL_EVENT) {
      tx_json("summary", "EXTERNAL_EVENT", "INFO", &r, vbat, rate, pump_on);
    }
  } else {
    ev_name = ag_event_name(fault);  // hard fault: isolate, don't analyse
    tx_json("alert", ev_name,
            fault == AG_EV_SENSOR_SPIKE_BURST ? "LOW" : "MED", &r, vbat, rate,
            pump_on);
    note_alert(ev_name, "LOW");
    if (fault != AG_EV_SENSOR_SPIKE_BURST) tx_flush();
  }

  // ---- summaries: fixed 15 min, adaptive sends every 4th unless active ----
  if (uptime_s() - rtc_last_summary_up > AG_SUMMARY_EVERY_S) {
    rtc_last_summary_up = uptime_s();
    rtc_summary_div++;
    bool active = rtc_core.cusum_up > 0.1f || rtc_core.cusum_down > 0.1f ||
                  rtc_core.state != AG_ST_NORMAL;
    if (active || (rtc_summary_div % AG_ADAPTIVE_DIV) == 0) {
      ag_result_t rs = {};
      tx_json("summary", NULL, NULL, &rs, vbat, rate, pump_on);
      tx_flush();
    }
  }

  // ---- probe scheduler: periodic audit + stress-triggered diagnosis ----
  // NEVER runs without a primed water loop (AG_PROBE_EVERY_S duty). A fresh
  // WATER_STRESS verdict also interrogates the field, with a 30-min
  // cooldown so a latched ALARM can't pump every evaluation.
  bool stress_now = (ev_name && !strcmp(ev_name, "WATER_STRESS"));
  if (man < 0 && !rtc_probing && !pump_on &&
      (uptime_s() - rtc_last_probe_up > AG_PROBE_EVERY_S ||
       (stress_now && uptime_s() - rtc_last_probe_up > 1800))) {
    probe_cycle(false);
    last_interact_ms = millis();
  }

  // ---- UI ----
  ui_snapshot_t us = {};
  us.moisture = moist; us.flow_lpm = rate; us.vbat = vbat;
  us.temp_c = dht_ok ? t : NAN; us.rh_pct = dht_ok ? rh : NAN;
  if (WiFi.status() == WL_CONNECTED) {
    strncpy(us.ssid, WiFi.SSID().c_str(), sizeof(us.ssid) - 1);
    strncpy(us.ip, WiFi.localIP().toString().c_str(), sizeof(us.ip) - 1);
  } else {
    strncpy(us.ssid, "no-wifi", sizeof(us.ssid) - 1);
    strncpy(us.ip, "--", sizeof(us.ip) - 1);
  }
  us.state = rtc_core.state;
  us.event = ev_name ? ev_name
             : (r.event != AG_EV_NONE ? ag_event_name(r.event) : "OK");
  us.pump_on = pump_on;
  us.leak_pulses = lk_pulses;
  us.alert0 = alert_ring[0]; us.alert1 = alert_ring[1]; us.alert2 = alert_ring[2];
  { uint32_t st = tx_sent_total(), sp = tx_suppressed_total();
    us.bw_saved_pct = (st + sp) ? (100.0f * sp / (st + sp)) : 0; }
  us.next_tx_s = (rtc_last_summary_up + AG_SUMMARY_EVERY_S > uptime_s())
                     ? (rtc_last_summary_up + AG_SUMMARY_EVERY_S - uptime_s())
                     : 0;
  ui_update(&us);
  ui_power_tick(rtc_core.state, rtc_probing, last_interact_ms);
  if (man >= 0)  // manual session: stream every evaluation (1 s)
    ws_send_flow(uptime_s(), rate, flow_volume_ml(), pump_on);

  // ---- sleep: deep in NORMAL, light delay otherwise ----
  // Honest deviation from §5.1's comment: on the S3 the PCNT is NOT an RTC
  // peripheral, so it cannot count through deep sleep (that holds for the
  // classic ESP32 + ULP, not the S3). Detection still works — a continuous
  // leak keeps flowing after wake and trips within ~2 cycles — but volume
  // integrates only while awake. Probe cycles (the only place volume
  // matters) never sleep, so conservation is unaffected.
  // ---- no deep sleep in this build: sleeping powers down the backlight
  // rail and the TFT goes dark every cycle. Mains-powered node, so we stay
  // awake on a light delay instead (cadence still adaptive).
  (void)cadence;
  delay(100);
}
