/** Runtime server-IP config: set from Serial Monitor, persists in NVS.
 * No reflash needed when the laptop IP changes.
 *
 * Serial commands (115200 baud, Newline):
 *   help              show all commands
 *   ip                show current server IP + URLs
 *   set ip <a.b.c.d>  set server IP (also accepts "ip <a.b.c.d>",
 *                     "ip=<a.b.c.d>", or a bare "192.168.1.50")
 *
 * Applies immediately (HTTP uses it on next flush, WS reconnects) and
 * survives reboots via NVS namespace "ag-net", key "srv_ip". */
#pragma once
#include <stddef.h>

void server_cfg_init(void);
const char *server_cfg_ip(void);
void server_cfg_url(char *out, size_t n);
bool server_cfg_valid_ip(const char *ip);
bool server_cfg_set(const char *ip);
void server_cfg_print(void);
// Non-blocking: call every loop(). Returns true if the IP changed.
bool server_cfg_poll_serial(void);
