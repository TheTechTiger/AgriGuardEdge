/**
 * ws_link.cpp — Links2004 WebSockets client to ws://<server_cfg_ip>:8765.
 * Hello on connect; pump commands in ({type:pump,on}); flow frames out;
 * ping from server refreshes the dead-man timer. All parsing is
 * fixed-shape + bounded (StaticJsonDocument<160>).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "ws_link.h"
#include "config.h"
#include "sensors.h"
#include "server_cfg.h"

static WebSocketsClient s_ws;
static int8_t s_manual = -1;
static uint32_t s_since = 0, s_rx = 0;
static uint32_t s_up_base = 0;  // uptime_s - millis()/1000, synced in tick
static bool s_want_radio = false;
static bool s_linked_once = false;
static bool s_begun = false;
static uint32_t s_began_ms = 0;
static char s_host[32] = "";

static uint32_t up_now(void) { return s_up_base + millis() / 1000; }

static void ws_hello(void) {
  char b[96];
  snprintf(b, sizeof(b), "{\"role\":\"esp\",\"device\":\"%s\"}", AG_DEVICE_ID);
  s_ws.sendTXT(b);
}

static void on_event(WStype_t type, uint8_t *payload, size_t len) {
  uint32_t now = up_now();
  switch (type) {
  case WStype_CONNECTED:
    s_want_radio = true;
    s_linked_once = true;
    ws_hello();
    break;
  case WStype_TEXT:
    s_rx = now;
    {
      StaticJsonDocument<160> d;
      if (deserializeJson(d, payload, len)) break;
      const char *t = d["type"];
      if (t && !strcmp(t, "pump")) {
        bool on = d["on"];
        s_manual = on ? 1 : 0;
        s_since = now;
        if (!on) pump_cmd(false);  // off takes effect immediately
      }
      /* ping: rx refresh only (see above) */
    }
    break;
  case WStype_DISCONNECTED:
    s_want_radio = false;
    break;
  default:
    break;
  }
}

void ws_init(void) {
  strncpy(s_host, server_cfg_ip(), sizeof(s_host) - 1);
  s_host[sizeof(s_host) - 1] = 0;
  s_ws.begin(s_host, AG_WS_PORT, "/");
  s_ws.setReconnectInterval(5000);
  s_ws.enableHeartbeat(12000, 4000, 2);
  s_ws.onEvent(on_event);
  s_want_radio = true;  // keep WiFi up: mains-powered demo policy
}

void ws_tick(uint32_t uptime_s) {
  s_up_base = uptime_s - millis() / 1000;
  // Live server-IP switch (Serial "set ip"): reconnect the WS client when
  // the stored IP changed. HTTP needs no handling (URL built per flush).
  if (strcmp(s_host, server_cfg_ip()) != 0) {
    strncpy(s_host, server_cfg_ip(), sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = 0;
    s_ws.disconnect();
    s_ws.begin(s_host, AG_WS_PORT, "/");
    s_ws.setReconnectInterval(5000);
    s_ws.enableHeartbeat(12000, 4000, 2);
    s_ws.onEvent(on_event);
    Serial.printf("{\"ws_reconnect\":\"%s\"}\n", s_host);
  }
  // Begin WiFi ONCE per outage: re-calling begin() every pass resets the
  // association and it never connects (found on hardware). Re-begin only
  // on explicit failure or a 20 s stall (covers post-sleep radio-off).
  if (WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (!s_begun || WiFi.status() == WL_CONNECT_FAILED ||
        now - s_began_ms > 20000) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(AG_WIFI_SSID, AG_WIFI_PASS);
      s_begun = true;
      s_began_ms = now;
    }
    return;  // loop() runs every ~50 ms; association progresses
  }
  s_ws.loop();
  // Dead-man + max-on safety (uses wall time; loop runs while awake).
  if (s_manual == 1 &&
      (uptime_s - s_since > AG_PUMP_TEST_MAX_S || uptime_s - s_rx > 15)) {
    s_manual = -1;
    pump_cmd(false);
    Serial.println("{\"pump\":\"auto-off\"}");
  }
}

bool ws_wants_radio(void) { return s_want_radio; }
bool ws_linked_once(void) { return s_linked_once; }
int8_t ws_manual(void) { return s_manual; }

void ws_send_flow(uint32_t uptime_s, float lpm, float vol_ml, uint8_t pump) {
  if (!s_ws.isConnected()) return;
  char b[160];
  snprintf(b, sizeof(b),
           "{\"type\":\"flow\",\"device\":\"%s\",\"t\":%u,"
           "\"flow_lpm\":%.2f,\"vol_ml\":%.0f,\"pump\":%d}",
           AG_DEVICE_ID, uptime_s, lpm, vol_ml, pump ? 1 : 0);
  s_ws.sendTXT(b);
}
