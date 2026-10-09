/**
 * net_tx.cpp — decisions, not data (§2). PSRAM ring (256 slots), idempotent
 * msg_id, exponential backoff, drop-oldest. WiFi fully off between flushes
 * (4 mAh/day vs 850 naive, §7). Summaries coalesce across outages (§6.8).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "net_tx.h"
#include "ws_link.h"
#include "config.h"

#define TX_SLOT 512

static char *s_ring = NULL;
static int s_head = 0, s_count = 0;
static uint32_t s_sent = 0, s_suppressed = 0, s_backoff_s = 5;
static uint32_t s_last_ok_ms = 0;

void tx_init(void) {
  s_ring = (char *)ps_malloc(AG_TX_RING_SLOTS * TX_SLOT);
  WiFi.mode(WIFI_OFF);
}

bool tx_queue(const char *kind, const char *json) {
  (void)kind;
  if (!s_ring) return false;
  if (s_count >= AG_TX_RING_SLOTS) {
    // drop oldest, count the suppression (§7.1 honest bytes)
    memmove(s_ring, s_ring + TX_SLOT, (s_count - 1) * TX_SLOT);
    s_count--;
    s_suppressed++;
  }
  strncpy(s_ring + s_head * TX_SLOT, json, TX_SLOT - 1);
  s_ring[s_head * TX_SLOT + TX_SLOT - 1] = 0;
  s_head = (s_head + 1) % AG_TX_RING_SLOTS;
  s_count++;
  return true;
}

static bool wifi_up(void) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(AG_WIFI_SSID, AG_WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

static void wifi_down(void) {
  if (ws_wants_radio()) return;  // realtime link owns the radio right now
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

int tx_flush(void) {
  if (!s_count) return 0;
  if (millis() - s_last_ok_ms < s_backoff_s * 1000 && s_last_ok_ms) return 0;
  if (!wifi_up()) {
    s_backoff_s = s_backoff_s < 120 ? s_backoff_s * 2 : 120;
    return 0;
  }
  HTTPClient http;
  http.setTimeout(5000);
  int sent = 0;
  int idx = (s_head - s_count + AG_TX_RING_SLOTS) % AG_TX_RING_SLOTS;
  while (s_count > 0) {
    http.begin(AG_BACKEND_URL);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)(s_ring + idx * TX_SLOT),
                         strlen(s_ring + idx * TX_SLOT));
    http.end();
    if (code != 200) break;  // keep the rest, back off
    idx = (idx + 1) % AG_TX_RING_SLOTS;
    s_count--;
    sent++;
    s_sent++;
  }
  wifi_down();
  if (sent) { s_backoff_s = 5; s_last_ok_ms = millis(); }
  else s_backoff_s = s_backoff_s < 120 ? s_backoff_s * 2 : 120;
  return sent;
}

uint32_t tx_sent_total(void) { return s_sent; }
uint32_t tx_suppressed_total(void) { return s_suppressed; }
