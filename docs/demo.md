# Demo script — AgriGuard Edge dashboard (5–6 min)

Read this verbatim during the demo. Every button injects the exact rows a
real node would emit via `/api/ingest` (tagged `demo:true` in evidence —
honest labeling, arch §11). On real hardware these same steps happen
physically and this file's §3 applies instead.

## 1. Prerequisites (once, ~2 min before the slot)

```sh
pip install -r AgriGuardEdge_Website/requirements.txt
python3 AgriGuardEdge_Website/app.py        # :82, boots from any directory
# open http://localhost:82/login → test@test.test / 12345678
```

- Log in, open the dashboard, **click `Reset demo data`**, then click
  `1 · Baseline soak` once — this seeds ~7.5 h of history so every panel
  renders. Confirm all 7 panels populated before the judges arrive.
- Browser fullscreen, 100 % zoom. Phone hotspot ready as WiFi fallback.
- Optional second terminal for credibility (run while talking):
  `g++` unit tests (31/31) · replay harness (12/12, 0 FA/day).

## 2. Procedure — click, point, narrate

**Orient (30 s).** "Seven panels, one detector — the same C++ that runs on
the ESP32 produced every number here." Sweep the hand: trace → conservation
→ flow → CUSUM → alerts → energy → scenarios.

**Step 1 — Baseline soak (45 s).** Already seeded. Point at the soil trace:
"Before learning, this is just noise. The shaded band is the learned
24-hour baseline ±2σ — normal lives *inside* the band. A naive rolling-mean
detector alarms every hot afternoon; hour-of-day buckets learn what 3 pm
looks like on this field." Point at CUSUM ladder: flat at NORMAL.

**Step 2 — Normal probe, 250 mL (45 s).** Click `2 · Normal probe`.
Conservation chart gains a bar: "The node pumped exactly 250 mL and asked:
*where did the water go?* Blue is retained in soil, red sliver is
unaccounted — ratio 0.14 lost, retention 0.86, `IRRIGATION_OK`. This is the
control. Watch what changes when water goes missing."

**Step 3 — ★ Continuous leak (60 s).** Click `3 · Loosen fitting (pump OFF)`.
A red card lands. Expand evidence: "Pump OFF — but flow reads 1.4 L/min
against a 0.3 leak-ε. And look at z-soil: −0.4, *normal*. Moisture sensors
are structurally blind to this — a slow drip keeps the root zone 'normal'
forever. One threshold catches a 24/7 water loss. This is the highest-value
line in the project." Show the flow strip jumping over the leak-ε line.
Click **acknowledge**: "closes the operator loop."

**Step 4 — ★ Crack fitting + probe (45 s).** Click `4 · Crack fitting + probe`.
New conservation bar, mostly red: "Same 250 mL delivered — but the soil bar
collapsed to 38 mL. Ratio 0.835 lost — `LEAK_OR_BYPASS`, HIGH. Compare with
Step 2 side by side: same probe cycle, opposite verdicts. The physics, not
a threshold, decided." Expand evidence: z −3.4, cusum↓ 6.2, deficit 5.1.

**Step 5 — Kink hose (30 s).** Click `5 · Kink hose + probe`. "Pump ON, flow
≈ 0 → `BLOCKAGE_NO_FLOW`. Plumbing honesty: a kink *after* the meter reads
as blockage, **not** a leak — leaks must tap after the meter."

**Step 6 — Unplug probe (30 s).** Click `6 · Unplug soil probe`. "Health gate
runs *before* analytics — ADC railed at 4095 → `SENSOR_DISCONNECTED` in one
pass. Untrusted data never reaches the baseline."

**Step 7 — Rain, the negative control (30 s).** Click `9 · Simulate rain`.
Trace spikes +8σ — and no red card: "`EXTERNAL_EVENT`, INFO. A detector that
alarms on rain isn't deployable. Rain blanking freezes the baseline for 48
evals — found by testing, documented in the errata."

**Step 8 — Energy (45 s).** Energy panel: "Readings generated vs messages
sent — ~95 % suppressed. ≈245 mAh/day, ≈10 days on one 18650. And honestly:
203 of those 245 are the *pump* — duty cycle, not compute, is the binding
constraint."

**Step 9 — Reproducibility (30 s).** Gesture at replay terminal: "One C++
file compiles for ESP32 and laptop. One command re-runs every number on
screen. `make all` — 12/12, rain silent, drift caught."

## 3. Conclusion — the real-hardware path (45 s)

"What you saw runs unmodified on silicon: `pio run` SUCCESS, RAM 44 %,
Flash 31.5 %. The rig is ESP32-S3 + one capacitive probe + one YF-S201 +
a 18650 — the detector doesn't change, only the plumbing does. Physically:
10-min soak → 250 mL probe (mark reference with 3 s BOOT hold) → loosen a
fitting into a bucket for the continuous leak → crack it for the severe
leak → the TFT shows this same conservation bar on-device with no cloud.
Bench → PCB with the level shifter integrated → solar top-up → a second
meter buys zone localisation. The scenario buttons you clicked are replaced
by water moving through a real pipe — every panel reads identical shapes."

## Reset between runs

`Reset demo data` wipes ingest-derived tables (messages/events/summaries/
energy/probe_results; auth untouched), then one `Baseline soak` click
re-seeds. Full reset-to-ready: two clicks, ~2 s.
