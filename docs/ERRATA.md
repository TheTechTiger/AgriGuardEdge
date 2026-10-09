# Build errata — places where implementation deliberately differs from `architechture.md`

Found by testing, not by opinion. Each item quotes the arch section it deviates from.

1. **§5.6 verdict bands apply to the RETENTION ratio, not `V_residual/V_del`.**
   The table is headed residual but every number (and the §8 example JSON:
   `V_del=250, V_soil=38.2, V_et=3.1, residual_ratio=0.834 ⇒ LEAK_OR_BYPASS`)
   only makes sense as `R = (V_soil + V_ET) / V_del`. The wire field keeps
   the §8 meaning; classification uses `R`. (`firmware/common/anomaly_core.h`)

2. **`K=1.0 / H=10`, not `K=0.5 / H=5`.** The arch says fit, don't guess —
   the sweep did: defaults gave **411 false alarms/day** on realistic probe
   noise (σ=0.8). `K=1.0/H=10` gives 0 FA/day with stress latency 2 evals
   and rain silent. 12/12, `make sweep` reproduces it.

3. **S3 PCNT does not count through deep sleep** (§5.1 comment claims it
   does — true for classic ESP32+ULP, not the S3; PCNT is not an RTC
   peripheral). Leak *detection* is unaffected (the leak keeps flowing after
   wake, trips within ~2 cycles); only in-sleep volume is lost, and probe
   cycles — the only place volume matters — never sleep. (`src/main.cpp`)

4. **Rain needs a blanking window** (§5.4 has none). The +8σ decay tail
   integrates in CUSUM_up (⇒ false `OVER_IRRIGATION`) and fast baseline
   adaptation toward rainwater fires `WATER_STRESS` when the rain ends.
   Fix: 48-eval blank, frozen baseline, sticky 3×`|z|<1` exit, CUSUM +
   persistence held, flow watches live. Rain still masks stress — stated.

5. **SUSPECT entry hardened** (§5.5 "≥3 of last 5 and ≥90 s" kept for
   promotion). A single 2–3σ excursion (p≈4 %/eval) entered SUSPECT and
   noise promoted ~10×/day. Entry now needs CUSUM confirmation or two
   consecutive excursions; votes counted fresh from entry; ≥4σ twice
   fast-tracks (meets ≤120 s for strong shifts, ~100 s for 3σ at SUSPECT
   cadence).

6. **ADC-rail check precedes calibrated-range check** (§5.2 lists range
   first). A disconnected probe floats to a rail AND reads out-of-range;
   the rail is the more specific signal. No host impact (`adc=-1` skips).

7. **Gate GPIOs GPIO7 (soil) / GPIO8 (flow power)** extend the §6.1 pin map,
   which gates both sensors but names no gate pins.

8. **Server lives in `AgriGuardEdge_Website/`** (workspace folder), not a
   separate `backend/` dir; same Flask/SQLite skeleton as the reference
   project plus the §8.1 tables and §9 API/routes/panels.

9. **Flow meter is a ZJ-S201 at 450 p/L, not 5888.** The arch's 5888
   matches no YF/ZJ-family datasheet (F = 7.5 × Q → 450 p/L, confirmed
   across vendors). At 450 p/L the rate is noisier at low flow — the EMA
   stage carries more weight — while stop-on-volume (250 mL ≈ 112 pulses)
   is unaffected. Bench calibration overwrites the nominal regardless.
10. **Soil probe is resistive, not capacitive.** DC electrolysis eats the
    electrodes if left powered, so GPIO7 gating is probe-preservation,
    not just power-saving. Expected slow upward drift over weeks lands
    exactly on the §5.6b `SENSOR_DRIFT` trip — the design already answers
    its own weakest sensor. Bone-dry air can pin the ADC near rail and
    correctly reads as `SENSOR_DISCONNECTED` (probe out of soil).
11. **No battery: 12 V pump on a wall adapter, node on USB/buck.**
    `cfg.batt_low_v` (default 3.4 V cell) is set to 10.5 V for the 12 V
    rail sensed through 100 k/27 k on GPIO2. §7's mAh table is kept as
    the battery-variant design reference; the demo reports measured
    supply current per policy + the 96 % bandwidth number.
12. **Summaries carry real window/energy/savings** (RTC accumulators
    across deep sleeps; mains SoC sent as 100, `mah_today` 0 — no current
    sensor, §1.3) instead of the zeros the first firmware sent.
13. **S3 + TFT_eSPI + Arduino 3.x display crash (fixed, diagnosed on
    hardware).** Two compounded causes: (a) Arduino renumbered `FSPI=0`
    while TFT_eSPI's register macros expect the IDF host id, so `init()`
    wrote SPI base `0+0x10` and Guru-panicked before `setup()` finished —
    fixed with `-DUSE_FSPI_PORT` (literal 2 = GPSPI2); (b) `MISO=-1`
    silently remaps MISO onto GPIO13 (our DC) as an INPUT — GPIO15 is
    sacrificed instead (leave unconnected). Found via addr2line, proven
    with a minimal probe sketch.
14. **LVGL 9 defaults `LV_USE_FLOAT=0`**, which compiles `case 'f'` out of
    its builtin printf: any `"%f … %s"` format desyncs varargs, `%s`
    reads the double bits (0.0) as a pointer, panic in `lv_strnlen(NULL)`.
    One-line fix in `lv_conf.h` (`LV_USE_FLOAT 1`); S3 has no FPU but the
    cost is irrelevant at one label per evaluation.
15. **This Arduino package ships no `pulse_cnt.h`** (ng driver), so flow
    uses the legacy `pcnt` driver (unit 0, int16 counter, 100-cycle
    glitch filter) — same peripheral, same claim. Boot log `"pcnt":1`
    proves it; ISR poll remains the fallback.
16. **Demo synthesis removed for the production lock-in.** Dashboard
    scenario buttons (synthetic node-shaped rows) and `/api/demo/reset`
    are gone; the launcher is a physical §11 checklist. Firmware demo
    flags (`AG_DEMO_DISPLAY_ON`, `AG_DEMO_LIVE_1S`) are 0, bandwidth
    percentage is computed from TX counters, summary scheduling is RTC,
    and probe cycles run on a 4-hour scheduler plus stress-triggered
    diagnosis (30-min cooldown) — the conservation gate finally runs
    on-device, not just in replay.
17. **Remote pump test over websocket (fully integrated).** ESP is a WS
    client of `ws://laptop:8765` (SSID/pass + laptop IP hardcoded in
    `config.h`); dashboard Pump Test card toggles the relay, flow
    streams back at 1 Hz. Node-side dead-man (120 s max, 15 s silence)
    and link-loss pump-off. Measured on hardware: ON → flow ramped
    0→0.27 L/min, 31 mL integrated, OFF clean — but 0.27 L/min is deep
    in the meter's sub-1 L/min blind zone (priming/head/supply to
    investigate; the 500 mL calibration quantifies it).
18. **WiFi.begin every loop pass never associates** (each call resets the
    handshake). Fixed with begin-once + re-begin on failure/20 s stall;
    plus a 3-min no-sleep grace so the first link establishes. Found
    because the ESP joined the LAN (192.168.1.27, pings flowing) only in
    a probe sketch with a single blocking begin.
19. **Sub-floor leaks via pulse integration (`MICRO_LEAK`).** Rate-based
    detection is blind below ~1 L/min, so the firmware integrates
    pump-OFF PCNT pulses over 15 min (≥20 ⇒ MED alert with pulse/window
    evidence + live `Lk` counter on the TFT). Verified end-to-end with a
    forced window (fired, ingested id 2119, rendered); production
    thresholds restored after. A fully motionless weep is still
    invisible — stated in §14.1.
