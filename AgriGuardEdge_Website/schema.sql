-- AgriGuard Edge schema. SQLite in WAL mode (see database.py).
-- Auth + notification shell, then the §8.1 edge-ingest tables.
CREATE TABLE IF NOT EXISTS "userDetails" (
	"id"	INTEGER NOT NULL,
	"userName"	TEXT NOT NULL,
	"emailId"	TEXT NOT NULL UNIQUE,
	"oAuth"	TEXT UNIQUE,
	"password"	TEXT NOT NULL,
	"profilePic"	BLOB,
	PRIMARY KEY("id" AUTOINCREMENT)
);
CREATE TABLE IF NOT EXISTS "notifications" (
	"id"	INTEGER NOT NULL,
	"userID"	INTEGER NOT NULL,
	"title"	TEXT NOT NULL,
	"description"	TEXT,
	"category"	TEXT NOT NULL DEFAULT 'info' CHECK("category" IN ('info', 'warn', 'success', 'danger')),
	"dateTime"	DATETIME DEFAULT (datetime('now', 'localtime')),
	"read"	INTEGER NOT NULL DEFAULT 0 CHECK("read" IN (0, 1)),
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS messages (            -- idempotent ingest
  msg_id TEXT PRIMARY KEY, received_at REAL NOT NULL,
  device_id TEXT NOT NULL, kind TEXT NOT NULL, payload TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY, device_id TEXT NOT NULL,
  received_at REAL NOT NULL, event TEXT NOT NULL, severity TEXT NOT NULL,
  state TEXT, evidence TEXT,                    -- JSON, incl. conservation terms
  ack_state TEXT DEFAULT 'UNACK');
CREATE TABLE IF NOT EXISTS summaries (
  id INTEGER PRIMARY KEY, device_id TEXT NOT NULL, received_at REAL NOT NULL,
  avg_moisture REAL, min_moisture REAL, max_moisture REAL, std_moisture REAL,
  avg_temp REAL, avg_rh REAL, vbat REAL, soc_pct REAL, mAh REAL);
CREATE TABLE IF NOT EXISTS energy_log (          -- makes §7 falsifiable
  received_at REAL, device_id TEXT,
  tx_count INT, tx_bytes INT, awake_s REAL, probe_cycles INT);
CREATE TABLE IF NOT EXISTS probe_results (       -- conservation audit trail
  received_at REAL, device_id TEXT, verdict TEXT,
  V_delivered_ml REAL, V_soil_delta_ml REAL, residual_ratio REAL,
  ratio_vs_reference REAL, flow_calibrated_ppl REAL);
CREATE INDEX IF NOT EXISTS idx_events_device ON events(device_id, received_at);
CREATE INDEX IF NOT EXISTS idx_probe_device ON probe_results(device_id, received_at);
-- demo login: test@test.test / 12345678 (change or delete after first use)
INSERT INTO "userDetails" ("userName","emailId","password") VALUES ('Demo','test@test.test','12345678');
