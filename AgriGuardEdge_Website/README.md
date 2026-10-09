# AgriGuardEdge_Website — Flask + SQLite dashboard

Mirrors the reference-project skeleton (auth, irrigation rules, AI chat,
ESP heartbeat) with AgriGuard Edge domain content: edge-ingest API,
conservation audit trail, and the 7-panel anomaly dashboard
(arch §8/§9).

## Setup

```sh
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env 2>/dev/null || printf 'SECRET_KEY=change-me\nOWM_API_KEY=\n' > .env
python3 app.py            # :82  (or ./run.sh)
```

Demo login (seeded): `test@test.test` / `12345678`. Optional: OpenWeatherMap
key (`OWM_API_KEY`) for the weather card, Ollama on `:11434` for chat and
AI irrigation rules — the anomaly pipeline needs neither.

## Edge API (what the firmware speaks)

| Route | Purpose |
|---|---|
| `POST /api/ingest` | Idempotent (`msg_id`) ingest: `alert \| summary \| probe_result \| heartbeat`; server stamps `received_at` |
| `GET /api/events` · `/api/summary` · `/api/energy` · `/api/probes` | Dashboard data (`?device_id=esp32_01`) |
| `POST /api/ack` | Acknowledge an alert (login required) |
| `POST /api/scenario` | Record a demo scenario on the timeline (login required) |
| `GET /health` | Liveness |

Kept from the skeleton: `/dashboard /login /register /profile /chat`,
`/irrigation-rules /periodic- /sensor-triggered- /manual- /auto-irrigation`,
`/api/chat* /api/auto-irrigate`, `/Get_ESPdata`, `/heartBeat`, `/assets/*`.

## Dashboard panels (arch §9)

Live soil trace + baseline band · conservation bars (delivered vs
retained + residual, coloured by verdict) · flow strip with leak-ε line ·
CUSUM state ladder · alert stream with expandable evidence + ack ·
energy/bandwidth (generated vs sent) · scenario launcher (§11).

## Storage

SQLite in WAL mode (`AgriGuardEdgeDB.db`, auto-created, git-ignored).
Schema: `AgroSenseDB.db.sql` (skeleton tables) + `agriguard_schema.sql`
(§8.1: `messages, events, summaries, energy_log, probe_results`).

## Notes

- The `wacli`/WhatsApp integration was removed (30 MB binary, unused).
- Every alert card carries its `evidence` JSON (conservation terms
  included) — undiagnosable alerts are a documented project failure mode.
- Ingest smoke test: `POST /api/ingest` with the §8 example payload, then
  check `/api/events` and `/api/probes`.
