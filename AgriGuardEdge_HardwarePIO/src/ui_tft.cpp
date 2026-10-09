/**
 * ui_tft.cpp — LVGL 9 on ST7735, mostly-black + redraw-on-change (§9.1).
 * Flush pattern inherited from the DeskBuddy bring-up; dmaWait() before sleep.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui_tft.h"
#include "config.h"

static const uint16_t SCR_W = 160, SCR_H = 128;
static TFT_eSPI tft = TFT_eSPI();
static lv_display_t *disp;
static lv_color_t buf1[160 * 32], buf2[160 * 32];
static volatile uint32_t s_flushes = 0;

static void my_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)px, w * h, true);
  tft.dmaWait();
  tft.endWrite();
  lv_display_flush_ready(d);
  s_flushes++;
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void blWrite(uint8_t d) { ledcWrite(PIN_TFT_BL, d); }
#else
#define BL_CH 0
static void blWrite(uint8_t d) { ledcWrite(BL_CH, d); }
#endif

// ---------- widgets ----------
static lv_obj_t *scr[4];
static lv_obj_t *liveMoist, *liveState, *liveFlow, *liveBatt, *liveTx;
static lv_obj_t *conBars[4];
static lv_obj_t *conRatio, *conVerdict;
static lv_obj_t *probeArc, *probeFlow, *probeInfo;
static lv_obj_t *alertLs[3];
static int s_cur = 0;
static char s_last[256];

static lv_obj_t *label(lv_obj_t *p, int x, int y, int w, int h,
                       const lv_font_t *f, uint32_t c) {
  lv_obj_t *o = lv_label_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_text_font(o, f, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
  return o;
}

static lv_obj_t *bar(lv_obj_t *p, int x, int y, int w, int h) {
  lv_obj_t *o = lv_bar_create(p);
  lv_bar_set_range(o, 0, 250);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_bar_set_value(o, 0, LV_ANIM_OFF);
  return o;
}

static uint32_t lvTickCb(void) { return millis(); }

void ui_init(void) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_TFT_BL);
#else
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(PIN_TFT_BL, BL_CH);
#endif
  blWrite(255);  // always-on build: full brightness from the first frame
  tft.init();
  tft.setRotation(3);
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);
  lv_init();
  lv_tick_set_cb(lvTickCb);
  disp = lv_display_create(SCR_W, SCR_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_flush);
  lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  for (int i = 0; i < 4; i++) {
    scr[i] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[i], lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr[i], LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(scr[i], 0, 0);
  }
  // ① LIVE
  liveState = label(scr[0], 2, 2, 70, 14, &lv_font_montserrat_10, 0x30FF70);
  liveBatt = label(scr[0], 80, 2, 78, 14, &lv_font_montserrat_10, 0x8AB4FF);
  liveMoist = label(scr[0], 0, 18, 160, 30, &lv_font_montserrat_28, 0xFFFFFF);
  liveFlow = label(scr[0], 2, 52, 156, 14, &lv_font_montserrat_12, 0x2BDFFF);
  liveTx = label(scr[0], 2, 100, 156, 26, &lv_font_montserrat_10, 0x8AB4FF);
  // ② CONSERVATION — the money screen
  label(scr[1], 2, 2, 156, 14, &lv_font_montserrat_10, 0x8AB4FF);
  const char *names[4] = {"Vdel", "Vsoil", "Vet", "Vres"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *nm = label(scr[1], 2, 20 + i * 16, 34, 12,
                         &lv_font_montserrat_10, 0xFFFFFF);
    lv_label_set_text(nm, names[i]);
    conBars[i] = bar(scr[1], 38, 20 + i * 16, 118, 10);
  }
  conRatio = label(scr[1], 2, 86, 156, 24, &lv_font_montserrat_20, 0xFFFFFF);
  conVerdict = label(scr[1], 2, 110, 156, 14, &lv_font_montserrat_12, 0xFF5030);
  // ③ PROBE
  probeArc = lv_arc_create(scr[2]);
  lv_arc_set_range(probeArc, 0, 250);
  lv_obj_set_size(probeArc, 80, 80);
  lv_obj_set_pos(probeArc, 40, 8);
  probeFlow = label(scr[2], 0, 92, 160, 20, &lv_font_montserrat_20, 0x2BDFFF);
  probeInfo = label(scr[2], 0, 112, 160, 14, &lv_font_montserrat_10, 0xFFFFFF);
  // ④ ALERTS
  for (int i = 0; i < 3; i++)
    alertLs[i] = label(scr[3], 2, 6 + i * 40, 156, 36,
                       &lv_font_montserrat_10, 0xFFFFFF);

  lv_screen_load(scr[0]);
  lv_timer_handler();
  s_last[0] = 0;
}

void ui_show(int screen) {
  s_cur = (screen < 0) ? 3 : (screen % 4);
  lv_screen_load(scr[s_cur]);
  lv_timer_handler();
}
int ui_current(void) { return s_cur; }

// Cold-boot splash: title + staged progress, ~1.6 s, then LIVE.
// Never called on deep-sleep wake (caller checks the wakeup cause).
void ui_boot_splash(uint32_t boot_n, int pcnt_ok) {
  blWrite(255);
  lv_obj_t *sp = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(sp, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(sp, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(sp, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(sp, 0, 0);
  lv_obj_t *t = label(sp, 0, 18, 160, 20, &lv_font_montserrat_14, 0x2BDFFF);
  lv_label_set_text(t, "AGRI GUARD EDGE");
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *st = label(sp, 0, 40, 160, 14, &lv_font_montserrat_10, 0x8AB4FF);
  lv_obj_set_style_text_align(st, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *bar = lv_bar_create(sp);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_pos(bar, 20, 62);
  lv_obj_set_size(bar, 120, 10);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  char b[48];
  snprintf(b, sizeof(b), "boot %u · flow %s", boot_n, pcnt_ok ? "PCNT" : "POLL");
  lv_obj_t *sub = label(sp, 0, 78, 160, 14, &lv_font_montserrat_10, 0xFFFFFF);
  lv_label_set_text(sub, b);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *msg = label(sp, 0, 100, 160, 14, &lv_font_montserrat_10, 0x30FF70);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_screen_load(sp);
  const char *steps[5] = {"sensors…", "flow meter…", "baseline…", "radio…", "live"};
  for (int i = 0; i <= 100; i += 5) {
    lv_bar_set_value(bar, i, LV_ANIM_OFF);
    lv_label_set_text(msg, steps[i / 20 > 4 ? 4 : i / 20]);
    lv_timer_handler();
    delay(80);
  }
  lv_screen_load(scr[0]);   // unload the splash FIRST …
  lv_obj_delete_async(sp);   // … then free it (never delete a loaded screen)
  s_cur = 0;
  s_last[0] = 0;  // force a full repaint on the first live update
  lv_timer_handler();
}

void ui_update(const ui_snapshot_t *s) {
  char b[256];
  snprintf(b, sizeof(b), "%d|%.1f|%.2f|%.2f|%.1f|%.0f|%.15s|%.31s|%u|%s|%.0f|%.0f|%.0f|%.0f|%.3f|%d|%u",
           s->state, s->moisture, s->flow_lpm, s->vbat, s->temp_c, s->rh_pct,
           s->ssid, s->ip, s->leak_pulses,
           s->event ? s->event : "-", s->v_del, s->v_soil, s->v_et,
           s->v_resid, s->ratio, s_cur, s->next_tx_s);
  if (!strcmp(b, s_last)) return;  // redraw-on-change only
  strcpy(s_last, b);

  const char *stt = s->state == 2 ? "ALARM" : (s->state == 1 ? "SUSPECT" : "OK");
  lv_label_set_text_fmt(liveState, "%s", stt);
  lv_obj_set_style_text_color(liveState,
      lv_color_hex(s->state == 2 ? 0xFF3030 : (s->state == 1 ? 0xFFC030 : 0x30FF70)), 0);
  lv_label_set_text_fmt(liveMoist, "%.2f L/m", s->flow_lpm);
  if (isnan(s->temp_c) || isnan(s->rh_pct))
    lv_label_set_text_fmt(liveFlow, "Soil %.1f %%  --C --%%RH", s->moisture);
  else
    lv_label_set_text_fmt(liveFlow, "Soil %.1f %%  %.1fC %.0f%%RH",
                          s->moisture, s->temp_c, s->rh_pct);
  lv_label_set_text_fmt(liveBatt, "%.1fV", s->vbat);
  lv_label_set_text_fmt(liveTx, "%s %s\ntx %us · %0.f%% · Lk%u", s->ssid, s->ip,
                        s->next_tx_s, s->bw_saved_pct, s->leak_pulses);

  lv_bar_set_value(conBars[0], (int)s->v_del, LV_ANIM_OFF);
  lv_bar_set_value(conBars[1], (int)s->v_soil, LV_ANIM_OFF);
  lv_bar_set_value(conBars[2], (int)s->v_et, LV_ANIM_OFF);
  lv_bar_set_value(conBars[3], (int)s->v_resid, LV_ANIM_OFF);
  lv_label_set_text_fmt(conRatio, "r %.3f (vs ref %.2f)", s->ratio, s->vs_ref);
  lv_label_set_text_fmt(conVerdict, "p%d %s", s->probe_n,
                        s->event ? s->event : "");

  lv_arc_set_value(probeArc, (int)s->vol_ml);
  lv_label_set_text_fmt(probeFlow, "%.1f L/m", s->flow_lpm);
  lv_label_set_text_fmt(probeInfo, "%s %lu pls dS %.2f",
                        s->pump_on ? "PUMP ON" : "pump off",
                        (unsigned long)s->pulses, s->dsoil_dt);

  if (s->alert0) lv_label_set_text(alertLs[0], s->alert0);
  if (s->alert1) lv_label_set_text(alertLs[1], s->alert1);
  if (s->alert2) lv_label_set_text(alertLs[2], s->alert2);

  lv_timer_handler();
}

void ui_power_tick(int state, int probe_active, uint32_t last_ms) {
  (void)state; (void)probe_active; (void)last_ms;
  // Always-on build: panel awake, backlight full. No dim, no sleep.
  ui_wake_panel();
  blWrite(255);
}

void ui_wake_panel(void) { tft.writecommand(ST7735_SLPOUT); }
