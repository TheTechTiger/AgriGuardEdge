# AgriGuardEdge_Website — Flask + SQLite dashboard

UI shell (NiceAdmin layout, auth, profile) plus the AgriGuard Edge domain:
edge-ingest API, conservation audit trail, and the 7-panel anomaly
dashboard (arch §8/§9). Nothing else — irrigation rules, AI chat, weather
and WhatsApp integrations were deliberately removed.

## Setup

```sh
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env  # then set a real SECRET_KEY
python3 app.py            # :82  (or ./run.sh, from any directory)
```

Demo login (seeded): `test@test.test` / `12345678` — change or delete
after first use.

## Routes (all of them)

| Route | Purpose |
|---|---|
| `/` → `/dashboard` | Dashboard (login required) |
| `/login` `/register` `/profile` `/profilePic` | Auth shell + avatar |
| `POST /mark-notification-read` | Header bell |
| `POST /api/ingest` | Idempotent (`msg_id`) ingest: `alert \| summary \| probe_result \| heartbeat`; server stamps `received_at` |
| `GET /api/events` · `/api/summary` · `/api/energy` · `/api/probes` | Dashboard data (`?device_id=esp32_01`) |
| `GET /api/status` | Node last-seen / online (from ingest, no legacy tables) |
| `POST /api/ack` | Acknowledge an alert (login required) |
| `GET /health` | Liveness |

## Realtime pump test (websocket hub :8765)

`app.py` also runs an asyncio hub (thread, `websockets` lib): the ESP
links as a **client** (`{"role":"esp"}`), browsers as UI (`{"role":"ui"}`).
Pump toggle → relay on the node; flow frames (1 Hz) stream back to the
Pump Test card. Server pings ESPs every 10 s; the node auto-stops the
pump after 120 s or 15 s of silence (dead-man switch, enforced on the
ESP, not the server). Local-demo trust: no auth on the socket.

## Dashboard panels (arch §9)

Live soil trace + baseline band (from summaries) · conservation bars
(from probe results, residual coloured by verdict) · flow strip (flow
evidence on alerts/probes, leak-ε line) · CUSUM state ladder · alert
stream with expandable evidence + ack · energy/bandwidth (generated vs
sent) · field demo guide (§11, physical steps only — no synthetic data).

## Storage

SQLite in WAL mode (`AgriGuardEdgeDB.db`, auto-created next to `app.py`,
git-ignored). One clean `schema.sql`: `userDetails, notifications` shell
+ §8.1 `messages, events, summaries, energy_log, probe_results`.

## Notes

- Every alert card carries its `evidence` JSON (conservation terms
  included) — undiagnosable alerts are a documented project failure mode.
- Ingest smoke test: `POST /api/ingest` with the §8 example payload, then
  check `/api/events` and `/api/probes`.
