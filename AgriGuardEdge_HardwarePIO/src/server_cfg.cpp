/**
 * server_cfg.cpp — runtime backend IP (Serial-settable, NVS-persistent).
 * Own NVS namespace "ag-net" so it never collides with main's
 * "agriguard" Preferences handle.
 */
#include <Arduino.h>
#include <Preferences.h>
#include "server_cfg.h"
#include "config.h"

#define SRV_NVS_NS "ag-net"
#define SRV_NVS_KEY "srv_ip"

static char s_ip[32] = AG_SERVER_HOST;
static char s_line[80];
static size_t s_len = 0;

bool server_cfg_valid_ip(const char *ip) {
  if (!ip || !*ip) return false;
  // Strict IPv4 dotted quad: 4 x 0-255, nothing else.
  int parts = 0;
  const char *p = ip;
  while (*p) {
    if (parts >= 4) return false;
    if (!isdigit((unsigned char)*p)) return false;
    long v = 0;
    int digits = 0;
    while (*p && isdigit((unsigned char)*p)) {
      v = v * 10 + (*p - '0');
      p++;
      if (++digits > 3 || v > 255) return false;
    }
    if (digits == 0) return false;
    parts++;
    if (*p == '.') {
      p++;
      if (!*p) return false;  // trailing dot
    } else if (*p) {
      return false;  // junk character
    }
  }
  return parts == 4;
}

void server_cfg_init(void) {
  Preferences pr;
  if (pr.begin(SRV_NVS_NS, true)) {  // read-only
    String v = pr.getString(SRV_NVS_KEY, "");
    pr.end();
    if (v.length() > 0 && v.length() < (int)sizeof(s_ip) &&
        server_cfg_valid_ip(v.c_str())) {
      strncpy(s_ip, v.c_str(), sizeof(s_ip) - 1);
      s_ip[sizeof(s_ip) - 1] = 0;
    }
  }
  // Self-heal: a stored value can never be invalid; fall back to default.
  if (!server_cfg_valid_ip(s_ip)) {
    strncpy(s_ip, AG_SERVER_HOST, sizeof(s_ip) - 1);
    s_ip[sizeof(s_ip) - 1] = 0;
  }
}

const char *server_cfg_ip(void) { return s_ip; }

void server_cfg_url(char *out, size_t n) {
  snprintf(out, n, "http://%s:82/api/ingest", s_ip);
}

bool server_cfg_set(const char *ip) {
  if (!server_cfg_valid_ip(ip)) return false;
  Preferences pr;
  if (!pr.begin(SRV_NVS_NS, false)) return false;
  pr.putString(SRV_NVS_KEY, ip);
  pr.end();
  strncpy(s_ip, ip, sizeof(s_ip) - 1);
  s_ip[sizeof(s_ip) - 1] = 0;
  return true;
}

void server_cfg_print(void) {
  char url[64];
  server_cfg_url(url, sizeof(url));
  Serial.printf("server ip : %s\n", s_ip);
  Serial.printf("http      : %s\n", url);
  Serial.printf("websocket : ws://%s:%d/\n", s_ip, AG_WS_PORT);
}

static void print_help(void) {
  Serial.println(F("commands:"));
  Serial.println(F("  help                 show this help"));
  Serial.println(F("  ip                   show server IP + URLs"));
  Serial.println(F("  set ip <a.b.c.d>     set server IP (saved, applies now)"));
  Serial.println(F("    also accepts: ip <a.b.c.d> | ip=<a.b.c.d> | bare 192.168.1.50"));
}

static void handle_line(char *line) {
  // Trim leading/trailing whitespace.
  while (*line && isspace((unsigned char)*line)) line++;
  size_t n = strlen(line);
  while (n && isspace((unsigned char)line[n - 1])) line[--n] = 0;
  if (!*line) return;

  if (!strcmp(line, "help") || !strcmp(line, "?") || !strcmp(line, "h")) {
    print_help();
    return;
  }
  if (!strcmp(line, "ip") || !strcmp(line, "show ip") ||
      !strcmp(line, "get ip") || !strcmp(line, "status")) {
    server_cfg_print();
    return;
  }

  // Extract candidate IP from: "set ip X" | "set server X" | "ip X" |
  // "ip=X" | "server X" | bare "X".
  const char *cand = NULL;
  if (!strncmp(line, "set ip", 6) && (line[6] == ' ' || line[6] == '=')) {
    cand = line + 7;
  } else if (!strncmp(line, "set server", 10) &&
             (line[10] == ' ' || line[10] == '=')) {
    cand = line + 11;
  } else if (!strncmp(line, "server", 6) && (line[6] == ' ' || line[6] == '=')) {
    cand = line + 7;
  } else if (!strncmp(line, "ip", 2) && (line[2] == ' ' || line[2] == '=' || line[2] == ':')) {
    cand = line + 3;
  } else if (isdigit((unsigned char)line[0])) {
    cand = line;  // bare address
  }
  while (cand && *cand && (isspace((unsigned char)*cand) || *cand == '=' || *cand == ':')) cand++;
  // Trailing junk after the address is not accepted.
  if (cand && *cand) {
    char tmp[40];
    strncpy(tmp, cand, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    // Trim trailing whitespace (leading already skipped).
    size_t m = strlen(tmp);
    while (m && isspace((unsigned char)tmp[m - 1])) tmp[--m] = 0;
    if (server_cfg_valid_ip(tmp)) {
      if (server_cfg_set(tmp)) {
        char url[64];
        server_cfg_url(url, sizeof(url));
        Serial.printf("ok: server ip set to %s (saved, applies now)\n", s_ip);
        Serial.printf("    http -> %s | ws -> ws://%s:%d/\n", url, s_ip, AG_WS_PORT);
      } else {
        Serial.println(F("err: NVS write failed"));
      }
    } else {
      Serial.printf("err: bad ip '%s' (want a.b.c.d, e.g. set ip 192.168.1.29)\n", tmp);
    }
    return;
  }

  Serial.printf("err: unknown cmd '%s' (try 'help')\n", line);
}

bool server_cfg_poll_serial(void) {
  const char *before = s_ip;
  (void)before;
  char prev[sizeof(s_ip)];
  strncpy(prev, s_ip, sizeof(prev));
  bool got = false;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_len] = 0;
      s_len = 0;
      handle_line(s_line);
      got = true;
    } else if (c == '\b' || c == 127) {
      if (s_len) s_len--;
    } else if (s_len + 1 < sizeof(s_line) && isprint((unsigned char)c)) {
      s_line[s_len++] = c;
    }
    // Drain at most a few dozen chars per pass so the 50 ms loop stays live.
    if (got) break;
  }
  return strcmp(prev, s_ip) != 0;
}
