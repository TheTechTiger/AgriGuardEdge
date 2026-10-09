/** Realtime pump-test channel. ESP is a websocket CLIENT of the laptop hub
 * (NAT-free on the LAN). Dashboard toggles relay here; flow streams back.
 * Safety: manual ON expires after AG_PUMP_TEST_MAX_S or 15 s without any
 * server contact (ping every 10 s) — relay forced off, override cleared. */
#pragma once
#include <stdint.h>

void ws_init(void);                                  // non-blocking connect
void ws_tick(uint32_t uptime_s);                     // call every loop()
bool ws_wants_radio(void);                           // true: keep WiFi up
bool ws_linked_once(void);                           // true since first WS hello
int8_t ws_manual(void);                              // -1 none, 0 hold-off, 1 hold-on
void ws_send_flow(uint32_t uptime_s, float lpm, float vol_ml, uint8_t pump);
