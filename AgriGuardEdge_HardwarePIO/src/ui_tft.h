/** ST7735 128×160 landscape + LVGL 9: 4 screens + backlight power manager. */
#pragma once
#include <stdint.h>

typedef struct {
  float moisture, flow_lpm, vbat;
  int state;               // 0 NORMAL 1 SUSPECT 2 ALARM
  const char *event;       // latest event name
  // conservation (screen 2)
  float v_del, v_soil, v_et, v_resid, ratio, vs_ref;
  int probe_n;
  // probe progress (screen 3)
  uint8_t pump_on;
  float vol_ml, vol_target;
  uint32_t pulses;
  float dsoil_dt;
  // alerts (screen 4): ring of last-3 strings rendered by app
  const char *alert0, *alert1, *alert2;
  float bw_saved_pct;
  uint32_t next_tx_s;
} ui_snapshot_t;

void ui_init(void);
void ui_boot_splash(uint32_t boot_n, int pcnt_ok);  // cold-boot animation only
void ui_show(int screen);          // 0 LIVE 1 CONSERVATION 2 PROBE 3 ALERTS
int  ui_current(void);
void ui_update(const ui_snapshot_t *s);  // redraw-on-change only
void ui_power_tick(int state, int probe_active, uint32_t last_interaction_ms);
void ui_sleep_panel(void);         // SLPIN + black, <100 uA check target
void ui_wake_panel(void);
