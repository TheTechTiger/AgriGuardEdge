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
