# AgriGuard Edge — Architecture

### Edge-Native Irrigation Anomaly Detection on an ESP32-S3
**24-hour hardware hackathon · v3 redraft (flow sensor integrated)**

---

## 0. TL;DR

A breadboarded ESP32-S3 node watches one resistive soil probe and one ZJ-S201 flow meter, with a 12 V pump on a wall adapter. It does **not** stream readings to the cloud. It builds a **self-calibrating, diurnal-conditioned baseline** of "normal" for the field, watches for **sustained departures** from it, and — the core idea — **interrogates the irrigation system**: it pulses its own pump through the flow meter and checks whether the delivered water actually appeared in the soil.

That gives it a **flow-mass-conservation gate**:

```text
        V_delivered  =  ∫ flow dt              (measured, ground truth)
        ΔV_soil      =  Δmoisture × V_wetted   (measured)
        V_ET         =  learned diurnal loss    (estimated)

        V_delivered  =  ΔV_soil + V_ET + V_leak + V_runoff
```

Everything above that identity is a **leak**. And when the identity is violated in the *impossible* direction — flow says water arrived, soil says it dried — **one of the two sensors is lying**, and we know *which one*. That is a sensor-fault detector that needs no labels, no dataset, and no self-referencing statistics. It is the part of this project we expect to be hardest to reproduce.

**Three things a reviewer will remember:**
1. **Flow as gravitational truth.** A physics conservation law turns a cheap soil probe into a self-validating instrument, and detects its own drift.
2. **Continuous-leak detection.** Pump off but flow non-zero → a silent 24/7 leak. *Undetectable by moisture or temperature ever.* One line of code, enormous practical value.
3. **One detector core, two runtimes** — the identical C++ runs on the ESP32 and on a host replay harness, so every claim is reproducible from a CSV in seconds.

On-board **ST7735 128×160 colour TFT (LVGL 9)** renders the conservation arithmetic live, so the demo works with no cloud and no laptop on the bench.

---

## 1. Constraints (what the 24-hour box actually gives us)

### 1.1 Available hardware

| Part | Interface | Design consequences |
|---|---|---|
| ESP32-S3 DevKitC-1 **N16R8** | — | 16 MB flash, 8 MB PSRAM, 12-bit ADC1, **PCNT** hardware pulse counters, WiFi 2.4 GHz |
| **Flow sensor ZJ-S201** | Pulse out, 5 V | 1–30 L/min, **450 pulses/L** (`F = 7.5 × Q`), ~10 mA. **5 V logic → level shifting mandatory.** Must be gated |
| Resistive soil moisture probe | Analog out | Dry ≈ rail, wet ≈ low. **DC electrolysis corrodes electrodes in days if left powered — must be power-gated** (§6.4); slow corrosion drift is caught by the conservation gate (§5.6b) |
| DHT11 | 1-wire GPIO | ±2 °C, **±20 % RH**, 0–50 °C, min 2 s between reads. Corroboration only |
| Relay module (+ driver) | GPIO | **Verify it clicks on 3.3 V logic** (many 5 V modules don't). COM/NO switch the 12 V+ pump line; flyback 1N4007 across pump + 1000 µF/25 V bulk cap; **common GND or nothing works** |
| 12 V pump | Mechanical | Runs off a **12 V wall adapter (≥ pump stall current)** — no battery in this BOM. Relay switches 12 V+, node never sees it |
| 12 V adapter + USB / buck | Power | Adapter → pump rail; node on USB 5 V (demo) or 12 V→5 V buck (field). 12 V rail monitored via 100 k/27 k on GPIO2 (<10.5 V ⇒ `POWER_LOW`) |
| Breadboard + jumpers | — | Contact bounce is real; verify wiring at H+2, not H+20 |

### 1.2 Flow sensor — the parameters that drive the design

| Property | Value | Why it matters |
|---|---|---|
| Output | **5 V TTL square wave** | **Exceeds ESP32-S3 GPIO abs-max (3.6 V). Level shift required — see §6.2.** This is the #1 way this build gets fried |
| Pulses per litre | **450** (`F = 7.5 × Q`) | `Hz = 7.5 × L/min` → 1 L/min = 7.5 Hz, 30 L/min = 225 Hz. **Bench-calibrate anyway (§5.7) — never trust the vendor number** |
| Range | **1–30 L/min** | **Sub-1 L/min leaks are invisible.** Addressed honestly in §14 |
| Supply | 5 V (tolerant 5–18 V) | From the 5 V rail (USB 5 V pin), gated via GPIO8 MOSFET/relay; fallback ungated with noted cost |
| Draw | ~10 mA | Gated to pump-on + leak checks — same lesson as the soil probe |
| Accuracy | verify by volume | **Unknown from the vendor page — the §5.7 calibration IS the datasheet.** Assume nothing |
| Mounting | **Arrow pointing up, vertical** | Horizontal or inverted mounting degrades the reading badly |

### 1.3 What we still do **not** have

| Missing | Consequence | Substitute |
|---|---|---|
| **Current / energy sensor** | Cannot *measure* energy per cycle | USB power meter for the §7 claims; flow infers **delivery**, not energy — stated plainly |
| Rain gauge | Cannot separate rain from irrigation | Rain appears as a large positive moisture step → **external-event detector** |
| Light / lux | Dataset has lux, hardware doesn't | Captured indirectly by hour-of-day bucket |
| Soil pH / TDS | Dataset has them, hardware doesn't | Omitted; dataset used for context and validation, not live features |
| Second flow meter | Cannot localise a leak to a specific emitter | Single-zone attribution only (§14) |
| SD card | No local logging on node | PSRAM ring buffer + **server-side timestamping** |
| Valve / relay state at the zone | No topology map | Probe cycles are *controlled*, so commanded state is known by construction |

### 1.4 Dataset reality (per PS)

La Trobe University, *Smart Agriculture and Plant Health Monitoring using IoT* — [Mendeley `65jxyrxv7b`](https://data.mendeley.com/datasets/65jxyrxv7b).

`env_temperature · env_humidity · light_intensity · solar_battery_voltage · soil_moisture · soil_temperature · soil_humidity · soil_pH · water_TDS`

**No flow channel. No irrigation on/off flag. No anomaly labels.** Said out loud, because it shapes the validation strategy: the dataset gives us the **diurnal shape we must not false-alarm on**; the flow sensor gives us **physics**; and §5.6's conservation law gives us **free sensor-fault labels** that the dataset cannot provide at all.

---

## 2. Problem statement

Battery-powered soil sensors across large farms must detect irrigation faults — leaks, sensor failure, blocked lines, pump failure, water stress — **locally**. Continuous uplink is unaffordable on a battery-class node: WiFi TX current is two orders of magnitude above sleep current, and a naive node sampling every 30 s with WiFi associated survives **under a day and a half** on a small cell. (This demo build is mains-powered — 12 V adapter + USB — so §7's battery table is the design reference and the reported metrics are measured supply current per policy plus bandwidth.)

AgriGuard Edge detects on the node and transmits **decisions, not data**.

---

## 3. Design principles, mapped to the judging rubric

| Criterion | Architectural commitment |
|---|---|
| **1. Problem understanding & depth** | Design derived from the exact parts list, including ZJ-S201's sub-1 L/min blind spot and resistive-probe corrosion. Power budget derived from real load currents, not message counts. |
| **2. Innovation & algorithmic uniqueness** | **Flow-mass-conservation gate** (§5.5) — physics-based fault isolation *and* label-free sensor-drift detection. Plus robust diurnal baseline, EWMA-CUSUM, N-of-M persistence, controlled-volume probing, adaptive cadence. |
| **3. Technical architecture & code quality** | One dependency-free core (`anomaly_core.h`), zero Arduino, compiled for **both** ESP32 and host. **PCNT hardware pulse counting**, not ISR polling. Versioned JSON, idempotent ingest. |
| **4. Working prototype** | MoSCoW with **kill checks at H+4, H+8, H+12**. Every feature has a documented degraded mode. Correct wiring tables. |
| **5. Defense poise, UI/UX** | ST7735 colour TFT with a **conservation screen** showing the algorithm's arithmetic live (demo works with no cloud), dashboard mirroring it, and **measured** energy numbers rather than assertions. Pre-written defense script (§13). |

---

## 4. System architecture

```text
┌───────────── 12 V adapter to pump rail + 5 V node rail (USB demo / buck field) ──────────────┐
│                                                                             │
│  ┌────────────┐   ┌─────────┐    ┌─────────┐   ┌────────┐   ┌──────────┐  │
│  │ ZJ-S201    │   │ Soil    │    │ DHT11   │   │ Relay  │   │  ST7735 TFT  │  │
│  │ 5 V pulses │   │ probe   │    │         │   │ +flyback│  │ 128×160      │  │
│  │  450 p/L   │   │ (gated) │    │         │   │        │   │ SPI + PWM BL │  │
│  └─────┬──▲───┘   └──┬───▲──┘    └────┬────┘   └───┬────┘   └────┬─────┘  │
│        │  │ GATE     │   │ GATE      │            │             │  SPI+PWM│
│        ▼  │          │   │           │            │             │        │
│   ┌────────────┐     │   │           │            │             │        │
│   │ 74AHCT125  │     │   │           │            │             │        │
│   │ 5V→3.3V    │     │   │           │            │             │        │
│   │ + Schmitt  │     │   │           │            │             │        │
│   └─────┬──────┘     │   │           │            │             │        │
│         │ GPIO6      │   │           │            │             │        │
│         └─────────────┴───┴────────────┴────────────┴─────────────┘        │
│                              │              │              │                  │
│                        ADC1  │           GPIO  │         SPI+PWM│
│                              ▼              ▼              ▼                  │
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │                     ESP32-S3  (240 MHz, 8 MB PSRAM)                    │ │
│  │                                                                       │ │
│  │   PCNT0 pulse accumulator ──┐                                        │ │
│  │   ADC + DHT11 driver ────────┤                                        │ │
│  │                             ▼                                        │ │
│  │   ┌────────────────────────────────────────────────────────────────┐  │ │
│  │   │  anomaly_core.h    ← PURE C++, NO Arduino, NO WiFi             │  │ │
│  │   │                                                                │  │ │
│  │   │  ① Sensor Health Gate          (incl. flow-channel checks)    │  │ │
│  │   │  ② Robust Diurnal Baseline + Residuals                        │  │ │
│  │   │  ③ EWMA-CUSUM Shift Detection                                │  │ │
│  │   │  ④ Persistence / Hysteresis State Machine                    │  │ │
│  │   │  ⑤ Controlled-Volume Probe Controller                         │  │ │
│  │   │  ⑥ ★ FLOW-MASS-CONSERVATION GATE                             │  │ │
│  │   └────────────────────────────────────────────────────────────────┘  │ │
│  │                             │                                         │ │
│  │   Ring buffer (PSRAM) ◄──────┤                                         │ │
│  │   Adaptive Cadence Controller │        WiFi: ON ONLY DURING TX         │ │
│  │            │                   │                                         │ │
│  │            ▼                   │                                         │ │
│  │   ┌──────────────────────────────────────────┐                         │ │
│  │   │  ST7735 128×160 (RGB565) + LVGL 9        │                         │ │
│  │   │  Backlight PWM-gated · redraw-on-change   │                         │ │
│  │   │  4 screens: LIVE / CONSERVATION / PROBE    │                        │ │
│  │   │          / ALERTS                         │                        │ │
│  │   └──────────────────────────────────────────┘                         │ │
│  └─────────────────────────────┼─────────────────────────────────────────┘ │
│                                │ SPI (SCK/MOSI/DC/CS/RST) + PWM backlight    │
│                                ▼                                          │
│                      Local UI — works with no cloud                        │
└────────────────────────────────┬────────────────────────────────────────────┘
                                 │ JSON over HTTP POST
                                 ▼
        ┌────────────────────────────────────────────────────────────┐
        │  Flask  ──►  SQLite (WAL)  ──►  Dashboard                 │
        │  /api/ingest      events, evidence,        live trace,    │
        │  /api/health      probe results, energy   CUSUM ladder,   │
        │  /api/ack         log                      conservation    │
        │                                           residual, savings│
        └────────────────────────────────────────────────────────────┘
```

### 4.1 The replay harness is a first-class component

```text
   Mendeley CSV  ──►  anomaly_replay (host)  ──►  metrics.csv
   + injected          SAME anomaly_core.h      precision / recall
   faults + synthetic                         F1 / latency
   flow profiles                                false alarms/day
                          ▲
                          │ identical source file
              firmware/src/main.cpp  ──►  ESP32-S3
```

The PS requires detection *from a dataset* on a *microcontroller-class simulator*. A host replay harness satisfies both, and it is how we get reproducible fault scenarios without physically breaking the rig twenty times. Every algorithm change validates in seconds instead of by reflashing.

---

## 5. The Edge Detector

All of it lives in `firmware/common/anomaly_core.h` — pure C++, no Arduino, no dynamic allocation beyond a caller-supplied arena.

### 5.1 Flow Front-End

Pulsed flow needs its own acquisition path, separate from the analog channels:

```c
// Hardware: PCNT0 unit 0 channel 0, rising-edge count on the level-shifted pin.
// Why PCNT and not an ISR: no ISR latency, no jitter, no missed edges at 1.5 kHz,
// and the count keeps accumulating while the CPU is deep-sleeping between samples.

typedef struct {
    uint32_t pulses;          // cumulative PCNT count
    uint32_t last_pulses;     // snapshot from previous evaluation
    uint32_t window_pulses;   // Δ over the current window
    uint32_t drops;           // missed-window counter → SENSOR_TIMEOUT
    uint32_t implausible;     // rate > 12 L/min (rig cap) → SENSOR_FAULT
    float    rate_lpm;        // instantaneous, smoothed
    float    volume_ml;       // ∫ rate dt, reset per probe cycle
    uint8_t  health;          // OK / STUCK_ZERO / SPIKE / IMPLAUSIBLE
} flow_ch_t;

// rate from pulses, then low-pass to kill pulse-quantisation noise
flow_ch_t flow_update(flow_ch_t *f, uint32_t pcnt_now,
                      float dt_s, float pulses_per_litre)
{
    uint32_t d = pcnt_now - f->last_pulses;      // wrap-safe
    f->last_pulses = pcnt_now;
    f->window_pulses += d;

    float litres   = (float)d / pulses_per_litre;
    float raw_lpm  = (dt_s > 0.f) ? (litres * 60000.0f / dt_s) : 0.f;

    f->rate_lpm += FLOW_ALPHA * (raw_lpm - f->rate_lpm);   // FLOW_ALPHA = 0.25
    f->volume_ml += litres * 1000.0f;

    // health — mirrors stage ①, applied to the flow channel
    if (f->rate_lpm < 0.f || f->rate_lpm > FLOW_RATE_MAX_LPM) f->implausible++;
    return *f;
}
```

**Quantisation note:** at 450 p/L, 1 L/min ≈ 7.5 pulses/s — volume integrates fine (250 mL ≈ 112 pulses) but the *rate* is noisy at low flow, which is why the EMA stage exists. The real limit is the sensor's mechanical floor (~1 L/min) and unquoted accuracy, both handled in §14. `pulses_per_litre` is **calibrated at runtime** (§5.7), not hard-coded from the datasheet.

### 5.2 Stage ① — Sensor Health Gate

**Never let untrusted data reach the analytics stage.** A flatlined or railing sensor must produce a *sensor fault*, not a confident irrigation anomaly.

| Channel | Check | Emits |
|---|---|---|
| Soil | calibrated moisture outside `[2 %, 98 %]` | `SENSOR_OUT_OF_RANGE` |
| Soil | raw ADC within 2 LSB of `0` / `4095` | `SENSOR_DISCONNECTED` |
| Soil | `MAD(Δx) < 0.15 %` for `K=8` consecutive evals | `SENSOR_STUCK` |
| Soil | `\|Δx\| > 6·MAD(Δx)` | `SENSOR_SPIKE_BURST` (sample excluded from baseline) |
| DHT11 | `NaN` / no edge in 5 ms | `SENSOR_TIMEOUT` |
| **Flow** | rate `< 0` or `> 12 L/min` (rig max 8 + margin) | `FLOW_IMPLAUSIBLE` |
| **Flow** | commanded ON but `rate ≈ 0` for 3 evals | `FLOW_NO_PULSE` |
| **Flow** | pulse count never advances for `> 60 s` | `FLOW_SENSOR_FAULT` |
| **Flow** | wildly varying rate while pump steady | `FLOW_NOISY` |
| Supply | rail below `cfg.batt_low_v` (cell 3.4 V / 12 V rail 10.5 V) | `POWER_LOW` |

> **Why MAD-based flatline, not exact-equal repeats?** Real resistive probes are noisy — and they corrode — exact repeats are *rare in healthy data*, so a "same value six times" test misses the actual failure mode. A disconnected probe floats to a rail; a bone-dry probe in air can pin near it too (read as out-of-soil, which is honest). We test **low innovation + out-of-range**, which is what really happens.
>
> **DHT11 caveat, handled explicitly:** ±20 % RH means it must never be a *primary* gate. It enters the feature vector as corroboration, below the soil channel in weight, with its own health gate.

### 5.3 Stage ② — Robust, Diurnal-Conditioned Baseline

**This is where the naive version fails.** A rolling mean tests each point against *itself*: soil moisture, air temperature and humidity all have strong daily cycles, and midday evapotranspiration produces a normal drying ramp of several %/hour. Any detector whose only reference is the last 30 minutes either alarms constantly or never alarms.

1. **Robust centre and scale** (outlier-tolerant, small-sample friendly):
   ```c
   med   = median(window);
   sigma = 1.4826 * median(|x - med|);   // MAD → σ-consistent for Gaussian data
   sigma = max(sigma, SIGMA_FLOOR);      // guard divide-by-zero on flat windows
   ```

2. **Hour-of-day conditioning** — a 24-bucket profile, not one mean:
   ```c
   float base_moist[24];   // EWMA per hour-of-day bucket
   float base_rh   [24];
   float et_rate   [24];   // expected drying rate %/h, learned in NORMAL
   ```
   An 08:00 bucket learns what 08:00 looks like *on this field this week*. Handles the diurnal cycle and slow seasonal drift for **288 bytes of RAM**.

3. **Guarded adaptation** — the baseline only learns from healthy samples:
   ```c
   if (health_ok && |residual| < SOFT_BAND)      // never learn from anomalies
       base[h] += ALPHA * (x - base[h]);          // ALPHA = 0.05
   ```
   Without the guard, a genuine slow leak is **absorbed into the baseline within hours** and the detector silently goes blind.

**Residual** `r_t = x_t − base[h_t]`, standardised `z_t = r_t / σ_h`. Note the **flow channel bypasses the baseline entirely** — it is a physical measurement, not a distributional one, so it is compared against hydraulic expectations instead.

### 5.4 Stage ③ — EWMA-CUSUM on the Residual

A single-sample z-score sees only *instantaneous* outliers. The faults that cost a farmer money are **sustained shifts**: a weeping emitter, a partially blocked line, a pump losing efficiency. CUSUM accumulates small consistent deviations and ignores noise:

```c
cusum_up   = fmaxf(0.0f, cusum_up   + (z - K));   // wetting / rising
cusum_down = fmaxf(0.0f, cusum_down + (-z - K));  // drying / stress
if (cusum_up > H || cusum_down > H) -> SUSPECT
```

**Memory: two floats.** Detection delay for a sustained `3σ` shift ≈ `H·T/3` ≈ **2.5 minutes** at 60 s cadence. `K` and `H` are **fit on the dataset via replay**, not guessed.

### 5.5 Stage ④ — Persistence & Hysteresis

One noisy ADC read must never page anyone.

```text
        ┌──────────┐  |z| > 2σ  OR  CUSUM > H_sus   ┌──────────┐
        │  NORMAL  │ ──────────────────────────────► │ SUSPECT  │
        │          │                                │  (burst  │
        └────▲─────┘                                │ sampling)│
             │                                        └──┬───┬──┘
             │ R clean evals              confirmed      │   │ resolved
             │                                           ▼   ▼
             │                                 ┌─────────┐ ┌────────┐
             └─────────────────────────────────┤  ALARM  │ │ NORMAL │
                    RECOVERY (3 clean evals)   └─────────┘ └────────┘
```

- Promote to `ALARM` on `≥3 of last 5` evaluations **and** `≥ 90 s` sustained.
- Severity: `LOW` (suspect > 10 min) → `MEDIUM` (confirmed) → `HIGH` (confirmed **and** `|z| > 3σ` **and** CUSUM tripped).
- Hysteresis is mandatory — separate promote/demote thresholds, or alerts flap.

### 5.6 ★ Stage ⑤ — Flow-Mass-Conservation Gate

**This is the project's technical core.** Over a controlled probe window, water delivered to the root zone must go somewhere specific:

```text
   V_delivered  =  ∫ flow dt                             ← measured, ground truth
   ΔV_soil      =  Δmoisture_pct × V_wetted_zone         ← measured
   V_ET         =  et_rate[h_t] × T_window               ← estimated from baseline
   V_runoff     ≈  0 during a short pulse                ← assumption, stated

   ⇒  V_residual = V_delivered − ΔV_soil − V_ET          ← unaccounted water
```

`V_residual` is then interpreted against the volume the system is *supposed* to retain:

| `V_residual / V_delivered` | Verdict | Severity |
|---|---|---|
| `0.80 – 1.15` | `IRRIGATION_OK` — water retained as expected | — |
| `0.45 – 0.80` | `EMITTER_PARTIAL` — partial blockage / poor distribution | MED |
| **`0.05 – 0.45`** | **`LEAK_OR_BYPASS`** — water delivered, not retained | **HIGH** |
| `< 0.05` with `V_delivered > 0` | `LEAK_SEVERE` — essentially all water lost | HIGH |
| **`> 1.15`** | **`OVER_IRRIGATION`** — retained more than delivered ⇒ **probe drift or broken emitter dumping locally** | MED/HIGH |

**Three things fall out of this that nothing else in the design can do:**

**(a) No absolute calibration needed.** `V_wetted_zone` is unknown, and honestly unknowable in 24 hours. But we don't need it — run one **reference cycle** on known-good hardware, record `R_ref = ΔV_soil / V_delivered`, and compare every subsequent cycle to `R_ref`. The ratio is self-calibrating. This mirrors the baseline philosophy applied to hydraulics.

**(b) Negative control against sensor drift.** If `R_t > 1.15`, more water appears in the soil than was ever delivered — **physically impossible**. Therefore the probe has drifted. This catches resistive-probe corrosion drift and calibration decay, which **no amount of self-referencing statistics can ever catch**, because a drifting probe defines its own "normal". Flow is an external reference that the probe cannot argue with.

**(c) Label-free sensor-fault ground truth.** The conservation identity yields labelled sensor faults *for free*, independent of the dataset. This partially answers the "your dataset has no anomalies" critique (§14).

**(d) ET correction matters and is included.** During a 2–3 minute pulse, evapotranspiration loss is small but non-zero and would bias `V_residual` positive. We subtract `et_rate[h]` learned from `NORMAL` windows — otherwise every hot afternoon reads as a leak.

### 5.7 Stage ⑥ — Controlled-Volume Probe + Continuous-Leak Watch

The passive detector can say *"moisture is not responding"*. It cannot say **why**. So the node **creates the excitation with a known input volume**:

```text
   ┌─ every N hours, OR triggered by water-stress deficit ─┐
   │                                                        │
   │  relay ON ──► integrate flow until V_target = 250 mL   │
   │        (stop-on-volume, not stop-on-time — a fixed     │
   │         volume makes every cycle directly comparable)   │
   │  relay OFF                                              │
   │        T_observe = 180 s  ◄── track d(moisture)/dt     │
   │        run conservation gate §5.6                       │
   │                                                        │
   │  ── meanwhile, pump OFF ──                              │
   │        if flow > ε_leak for 2 consecutive windows      │
   │            ⇒ CONTINUOUS_LEAK            ★ see below    │
   └────────────────────────────────────────────────────────┘
```

**`CONTINUOUS_LEAK` is the highest-value single rule in the project.** Pump off, yet water is moving → a fitting is weeping into the soil, a valve is passing, or a line is open. Moisture and temperature are *structurally blind* to this — a slow drip keeps the root zone within normal range indefinitely. One threshold detects it. For a farmer this is the difference between a 24/7 water loss and a fixed valve.

**Calibrating the pulse constant.** We do not trust the vendor's 450 p/L (F = 7.5 × Q):

```text
   1. Put the outlet in a graduated container (500 mL bottle).
   2. Run a timed manual flow; count pulses over the whole 500 mL.
   3. pulses_per_litre = counts / 0.5        ← written to config.h
   4. Re-run; expect ±2 %. Record it in the defense.
```

### 5.8 Anomaly Taxonomy

| Code | Source | Trigger | Sev |
|---|---|---|---|
| `CONTINUOUS_LEAK` | Flow | flow > ε while pump OFF | **HIGH** |
| `LEAK_SEVERE` | §5.6 | residual ratio < 0.05 | **HIGH** |
| `LEAK_OR_BYPASS` | §5.6 | residual ratio 0.05–0.45 | **HIGH** |
| `OVER_IRRIGATION` | §5.6 | residual ratio > 1.15 ⇒ probe drift / local dumping | HIGH |
| `BLOCKAGE_NO_FLOW` | Flow + §5.7 | pump ON, `flow ≈ 0` | **HIGH** |
| `BLOCKAGE_PARTIAL` | §5.6 | residual 0.45–0.80 | MED |
| `WATER_STRESS` | ②③④ | deficit vs. diurnal envelope sustained | HIGH |
| `SENSOR_STUCK` | ① | zero-innovation soil channel | HIGH |
| `SENSOR_OUT_OF_RANGE` / `DISCONNECTED` | ① | probe fault | MED |
| `SENSOR_DRIFT` | §5.6(b) | impossible mass balance ⇒ probe is wrong | HIGH |
| `FLOW_SENSOR_FAULT` / `FLOW_NO_PULSE` / `FLOW_IMPLAUSIBLE` | ① | flow channel untrustworthy | MED |
| `SENSOR_SPIKE_BURST` | ① | rejected transient | LOW |
| `EXTERNAL_EVENT` | ② | rain / third-party irrigation, large step | INFO |
| `POWER_LOW` | ① | supply rail below threshold | MED |

### 5.9 Adaptive Sampling Cadence

Radio *and* probe *and flow-meter* energy are dominated by how often we wake up. Wake frequency follows confidence:

| State | Cadence | Rationale |
|---|---|---|
| `NORMAL` | **60 s** | Baseline needs resolution, not frequency |
| `SUSPECT` | **10 s** | Confirm a shift before spending radio energy |
| `ALARM` / probe cycle | **1 s** (flow) + **5 s** (soil) | Capture the pulse response curve; 1 s is needed to resolve volume at low flow |
| Display | **off when `NORMAL` > 5 min since interaction; 15 % after 30 s; 100 % on `ALARM` or during a probe cycle** | Backlight is ~60 mA — the largest single display load (§7) |

The expensive regime is entered only when something is actually happening — which is what makes the power story defensible rather than asserted.

---

## 6. Hardware design

### 6.1 Pin map (ESP32-S3-DevKitC-1, N16R8)

| Function | GPIO | Notes |
|---|---|---|
| Soil probe → ADC | **GPIO1** (ADC1_CH0) | Resistive AO (dry ≈ rail, wet ≈ low). ADC2 is unusable while WiFi is active — another reason to power-gate WiFi |
| Soil probe VCC (gate) | **GPIO7** | 200 ms/sample. Mandatory on resistive — DC electrolysis eats electrodes in days if left on |
| Flow 5 V (gate) | **GPIO8** | MOSFET/2nd-relay gate for the ~10 mA meter; demo fallback = tie to 5 V rail |
| 12 V rail sense → ADC | **GPIO2** (ADC1_CH1) | 100 k / 27 k divider (×4.7037); gated with the probe; <10.5 V ⇒ `POWER_LOW` |
| DHT11 data | **GPIO4** | 4.7 k–10 k pull-up to 3V3; sample ≥ 2 s apart |
| Relay | **GPIO5** | **Verify the module clicks on 3.3 V logic.** COM/NO switch the 12 V+ pump line; flyback 1N4007 across pump + 1000 µF/25 V bulk cap; **common GND** |
| **Flow pulse (post-shift)** | **GPIO6** → **PCNT0 unit 0 ch 0** | Rising-edge count. `GPIO6` is ADC2_CH0 but that is irrelevant for a digital input |
| TFT backlight | **GPIO9** (LEDC PWM) | **Must be on a GPIO, not tied to 3V3** — this is the biggest power lever in the UI (§7) |
| TFT SCK | **GPIO12** | FSPI host |
| TFT MOSI | **GPIO11** | |
| TFT DC | **GPIO13** | |
| TFT CS | **GPIO10** | |
| TFT RST | **GPIO14** | |
| Calibration / ACK button | **GPIO0** | Boot button reused |

**Deliberately avoided:** `GPIO19/20` (USB D−/D+ — also SPI pins we want free for Serial/JTAG logs), `GPIO43/44` (UART0 — Serial monitor), `GPIO45/46` + `GPIO0/3` (strapping), `GPIO26–37` (octal PSRAM on N16R8).

### 6.2 Flow level shifting — do this before anything else

The ZJ-S201 output swings to **5 V**. ESP32-S3 GPIO abs-max is **3.6 V**. Connecting it directly destroys the pin, and the symptom (random resets, then a dead pin) gets misdiagnosed as a code bug at 3 a.m.

```text
   ZJ-S201 OUT ────┬─────────────────────────────► ESP32 GPIO6 (PCNT0)
                   │
                 10 kΩ         ← series
                   │
                   ├─────────────┐
                   │           20 kΩ   ← to GND  (5 V × 20/30 = 3.33 V)
                  GND            │
                   └─────────────┘
```

**Better:** a single **74AHCT125** quad buffer does level translation *and* provides Schmitt-trigger hysteresis, which materially improves pulse cleanliness on a noisy breadboard:

```text
   ZJ-S201 OUT ──► 74AHCT125 A_in      (5 V-tolerant input)
                  74AHCT125 A_out ──► ESP32 GPIO6
                  VCC = 5 V, VCCA = 3V3   (AHCT outputs 3.3 V CMOS)
```

If the AHCT is unavailable, the divider works — add a **10 k pull-up to 3V3** on the signal so the line is never floating when the sensor is unpowered.

### 6.3 Plumbing topology

```text
          12 V adapter ──┬──► ZJ-S201  (V+, 5 V rail)
                         │
          USB 5 V ───────┴──► ESP32-S3 (demo; field: 12 V→5 V buck)

   ┌───────────────── irrigation line ──────────────────┐
   │                                                     │
   │   12 V adapter ──► [12 V pump] ──► relay ──► [ZJ-S201] ──┼──► solenoid ──► zone
   │                                        ▲           │                   │
   │                                        │           │              soil probe
   │                              measured here        └───── probe zone ───┘
   │
   └── 1000 µF bulk cap across pump terminals; flyback diode across relay contacts
```

**Placement is the single highest-leverage plumbing decision**, and we change it per demo:

| Placement | Enables | Loses |
|---|---|---|
| **Downstream of pump, before the zone** (default) | Volume balance, pump/blockage faults, conservation gate | Cannot localise *which* emitter leaks |
| **Downstream of the solenoid** | Zone-local leaks; cleanest `CONTINUOUS_LEAK` test (valve commanded shut, meter reads zero) | Flow no longer proves pump health |
| **Two positions, swapped mid-demo** | Live demonstration of attribution | Extra plumbing time at H+10 |

**Mount the ZJ-S201 vertically with the arrow pointing up.** Horizontal or inverted mounting degrades the reading substantially — a silent accuracy loss that would quietly poison §5.6.

### 6.4 Power domains

```text
   12 V adapter ──┬── 12 V pump (via relay COM/NO)
                  └── (buck) ──► 5 V node rail (USB in the demo)
   5 V node rail ──┬── ESP32-S3
                   ├── ST7735 logic + backlight (PWM on GPIO9)
                   ├── resistive probe (GPIO7-gated — anti-corrosion, not just power)
                   ├── 12 V sense divider (gated) + DHT11
                   └── ZJ-S201 (GPIO8-gated)      ← ~10 mA, gated!
```

Four consequences that materially change the design:

1. **Gate the flow sensor.** At ~10 mA continuous it is the largest always-on load in the node. We only need it powered during pump-on windows and during periodic pump-off leak checks — same lesson as the soil probe, one level up: *the sensors, not the radio, are what kill a node.* (Battery-variant reference numbers retained in §7.)
2. **PWM-gate the TFT backlight.** At full white the ST7735 backlight pulls **~60 mA — comparable to the pump, and 4× the ESP32's entire processing budget.** Tying `LED` straight to 3V3 (the common default) burns **≈1 440 mAh/day** and dwarfs every other optimisation in this document. On `GPIO9` with LEDC PWM we get three levels: **off** (>5 min idle), **15 %** (ambient readout), **100 %** (alarm or active probe). See §9.1.
3. **The pump is 12 V and the node never sees it.** A 12 V wall adapter (≥ stall current) feeds the pump through the relay; the node runs on USB 5 V (demo) or a 12 V→5 V buck (field). No boost stage, no battery anywhere. If the adapter is unavailable by H+4, the documented fallback is a **bench 12 V supply shared with the buck** — every algorithmic claim still holds, stated up front.
4. **Pump inrush can brownout the ESP32.** Bulk cap + separate rail, or the node dies mid-test and gets misdiagnosed as a crash.

> **Mains safety:** the relay switches 12 V+, **not mains**. Keep the adapter brick off the breadboard, fuse the 12 V+ line if the adapter is unfused, and tie all GNDs together — a floating pump ground makes the relay chatter and the flow reading garbage.

### 6.5 Firmware structure

```text
firmware/
  common/anomaly_core.h    ← pure C++, the ENTIRE algorithm, no Arduino
  src/main.cpp             ← sensors, PCNT, scheduler, WiFi
  src/flow_pcnt.cpp        ← pulse capture + calibration + volume integration
  src/ui_tft.cpp           ← LVGL 9 screens + backlight power manager
  include/lv_conf.h        ← LVGL config (from DeskBuddy, trimmed)
  test/                    ← native unit tests over fixture vectors
tools/anomaly_replay/      ← host replay harness over the dataset
backend/                   ← Flask + SQLite + dashboard
```

`main.cpp` owns I/O and scheduling; `anomaly_core.h` owns all decisions. This split is what makes §4.1 work and keeps the algorithm testable without hardware.

### 6.6 Display stack — inherited from DeskBuddy, adapted

We reuse the proven **TFT_eSPI + LVGL 9** stack from `DeskBuddy`, configured through `platformio.ini` build flags (no `User_Setup.h` editing). Changes for AgriGuard:

```ini
; AgriGuardEdge_HardwarePIO/platformio.ini
[env:esp32s3_n16r8]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_upload.flash_size = 16MB
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi   ; 8MB octal PSRAM
board_build.psram_type = opi
monitor_speed = 115200

lib_deps =
  bodmer/TFT_eSPI@^2.5.43
  lvgl/lvgl@^9.2.2
  bblanchon/ArduinoJson@^7.4.2

build_flags =
  -DBOARD_HAS_PSRAM -DCONFIG_SPIRAM_SUPPORT=1
  -Iinclude
  -DUSER_SETUP_LOADED=1
  -DST7735_DRIVER=1
  -DTFT_WIDTH=128 -DTFT_HEIGHT=160
  -DST7735_BLACKTAB=1
  -DTFT_RGB_ORDER=1
  -DTFT_MOSI=11 -DTFT_SCLK=12 -DTFT_CS=10 -DTFT_DC=13 -DTFT_RST=14
  -DTFT_BL=9                    ; ← backlight on GPIO9 (DeskBuddy uses -1)
  -DTFT_MISO=-1
  -DSPI_FREQUENCY=20000000      ; 20 MHz, not 40 — see note
  -DUSE_DMA=1
  -DLOAD_FONT2=1 -DLOAD_GFXFF=1 -DSMOOTH_FONT=1
  -DLV_CONF_INCLUDE_SIMPLE=1
```

**Three deviations from DeskBuddy, all deliberate:**

| Change | Reason |
|---|---|
| `-DTFT_BL=9` instead of `-1` | Backlight must be **PWM-gated**. With `TFT_BL=-1` the LED is tied to 3V3 and costs ~1 440 mAh/day (§7) |
| `SPI_FREQUENCY` 40 → **20 MHz** | DeskBuddy runs short, tidy wires. AgriGuard is on a **breadboard with dupont jumpers**, where 40 MHz produces visible colour artefacts. 20 MHz is the reliable setting; try 27 MHz if the jumpers are short |
| Add `board_build.psram_type = opi` | S3 N16R8 has octal PSRAM; DeskBuddy was a plain `esp32dev` with none |

`lv_conf.h` is DeskBuddy's with two changes: **`LV_DEF_REFR_PERIOD` 30 → 200 ms** (we need ~5 Hz, not 33 FPS — a sensor node has no business rendering at 33 FPS) and **`LV_USE_ANIMATION 0`** (animating values that update once a minute is pure waste). Keep RGB565, 64 KB heap, `LV_USE_ARC` (perfect for the probe volume target), and the Montserrat 10/12/14/20/28 font set.

### 6.7 Two display-specific correctness traps

```c
// 1. DMA must complete before deep sleep, or you wake to a torn frame
//    (and sometimes crash on a half-finished SPI transaction).
tft.dmaWait();
lv_deinit();            // or leave LVGL up; either way, no in-flight DMA
esp_deep_sleep_start();

// 2. Backlight off is NOT screen off. The ST7735 keeps driving pixels
//    and still draws ~5-15 mA on a mostly-black UI. For long sleeps
//    send the panel to sleep too:
tft.fillScreen(TFT_BLACK);
tft.writecommand(ST7735_SLPIN);   // panel driver off, ~µA
// ... on wake:
tft.writecommand(ST7735_SLPOUT);
```

Backlight-off alone leaves 5–15 mA flowing indefinitely. Both, or the "screen off" claim is not true.

### 6.8 Connectivity & failure behaviour

| Condition | Behaviour |
|---|---|
| WiFi down | Queue in PSRAM ring buffer (256 slots), exponential backoff, **drop oldest** on overflow |
| Backend down | Summaries coalesce so a long outage costs one message, not ninety-six |
| Clock unknown | Device sends **`uptime_ms` + `boot_count`**; server stamps `received_at`. No NTP per wake — an NTP handshake on a deep-sleeping battery node is pure waste, and RTC drift makes device timestamps untrustworthy anyway |
| Flow sensor unplugged | `FLOW_SENSOR_FAULT` → §5.6 gate disabled, moisture-only detection continues, conservation checks marked `UNAVAILABLE` in the payload |
| Duplicate delivery | `msg_id` + `UNIQUE` constraint, server-side idempotency |

---

## 7. Power budget

**Battery-variant reference budget — the demo build is mains-powered** (12 V adapter + USB), so what we *report* is measured supply current per policy on a USB meter plus the bandwidth number. The table below is kept as the design reference: it is what sizes the battery if this ever goes off-grid. Daily figures assume 60 s `NORMAL` cadence, 96 summaries, ~20 min/day of screen interaction, 6 probe cycles/day, 2600 mAh protected cell, ~85 % boost efficiency. Expressed as **mAh drawn from the cell**.

| Load | Active | Naive (per day) | Gated (per day) | Mitigation |
|---|---|---|---|---|
| **ZJ-S201 flow sensor** | ~10 mA @ 5 V | **~360 (ref)** | **~5 (ref)** | power-gate to pump-on + periodic leak checks |
| **TFT backlight** | 60 mA @ 3V3 | **1 699** | **19** | PWM on GPIO9: off / 15 % / 100 % |
| **Soil probe** | 5 mA | 142 | **0.8** | power-gate, 200 ms burst per sample |
| **WiFi** | 240 mA bursts | 850 | **4** | connect → send → disconnect; radio fully off between |
| **TFT panel logic** | 5–15 mA | 240 | **1** | mostly-black UI + `ST7735_SLPIN` between wake-ups |
| **LVGL render CPU** | 50 mA while drawing | 120 | **2** | redraw-on-change only, `LV_DEF_REFR_PERIOD` 200 ms |
| ESP32 awake (sense + compute) | 50 mA | 9 | **9** | deep sleep between samples |
| DHT11 | 2.5 mA | 0.2 | **0.2** | — |
| **Node electronics subtotal** | | **~3 546** | **≈ 42** | **≈ 84× reduction** |
| Mini pump (6 × 3 min) | 500 mA @ 5 V | 203 | 203 | duty-cycle limiting |
| **Total** | | **~3 749** | **≈ 245** | |

**Headline, stated honestly:**

| Policy | mAh/day | Est. life on 2600 mAh |
|---|---|---|
| Naive (always-on WiFi, sensors + backlight ungated, per-reading uplink) | ~3 749 | **~0.7 days** |
| Naive + duty-cycled TX only | ~2 900 | ~0.9 days |
| **AgriGuard Edge** | **≈ 245** | **≈ 10 days at 6 cycles/day** |
| **AgriGuard Edge, 2 cycles/day** | **≈ 110** | **≈ 23 days** |

**The single most important number on this page:** the TFT backlight alone, wired the conventional way (`LED` → 3V3), costs **1 699 mAh/day — more than the pump, more than everything else combined.** Anyone who builds this on a breadboard will see "battery lasts 16 hours" and blame the firmware. PWM-gating one pin is worth more than every algorithmic optimisation in this document combined.

**The second honest finding:** after optimisation the **pump dominates — 203 of 245 mAh/day is pumping water, not sensing it.** So the binding constraint on runtime is *irrigation duty cycle*, not MCU compute or display. We report this plainly: the node electronics are effectively "free" against the hydraulic duty, and reducing pump cycles buys more life than any further firmware optimisation. That inverts the usual assumption and is worth saying out loud in the defense.

### 7.1 Bandwidth

The dataset's own reference deployment publishes **every reading** to ThingSpeak — our documented baseline, which makes the comparison defensible rather than hypothetical.

| | Messages/day | Bytes/day (@ ~240 B) |
|---|---|---|
| Baseline (per-reading uplink) | 2 880 @ 30 s | ~690 KB |
| **AgriGuard Edge** | 96 summaries + ~5–20 alerts ≈ **~110** | **~26 KB** |
| **Reduction** | **~96 %** | **~96 %** |

> **Correcting the earlier claim.** The v1 pitch said "up to 99 %". At 96 summaries/day the honest figure is **~96 %**; 99 % needs ~29 messages/day, reachable only with **adaptive summaries** (send every 4th summary unless CUSUM > 0 or an alert is active). We implement that as the default and report the measured value. **A defended 96 % beats an unsupported 99 %.**

---

## 8. Data contracts

```jsonc
POST /api/ingest
{
  "msg_id": "esp32_01:boot7:1841",     // idempotency key
  "device_id": "esp32_01",
  "boot_count": 7, "uptime_ms": 184100, // server stamps received_at
  "kind": "probe_result",               // alert | summary | probe_result | heartbeat

  "event": "LEAK_OR_BYPASS", "severity": "HIGH",
  "evidence": {
    // distribution side
    "z_soil": -3.4, "cusum_down": 6.2, "slope_30s": 0.00,
    "slope_300s": -0.11, "baseline_moist": 41.8, "sigma": 0.9, "deficit": 5.1,
    // conservation side — the part that makes the verdict defensible
    "V_delivered_ml": 250.0,
    "V_soil_delta_ml": 38.2,
    "V_et_ml": 3.1,
    "residual_ratio": 0.834,
    "ratio_vs_reference": 0.29,
    // instantaneous
    "flow_rate_lpm": 0.0, "pump_state": "OFF", "flow_calibrated_ppl": 450,
    // state machine
    "state": "ALARM", "state_age_s": 240, "cadence_s": 5,
    "degraded_modes": []
  },

  // summary
  "window": { "avg_moisture": 41.8, "min": 33.2, "max": 58.9,
              "std": 6.1, "avg_temp": 26.4, "avg_rh": 61.0 },
  "energy": { "vbat": 3.95, "soc_pct": 78, "mah_today": 229.4 },
  "savings": { "readings_generated": 1440, "messages_sent": 6,
               "suppressed": 1434 }
}
```

**Why `evidence` is non-negotiable:** without it a false positive can never be diagnosed and the dashboard can never explain an alert. For a project whose entire claim is *"my detector beats a threshold"*, being unable to show your reasoning is fatal.

### 8.1 Storage

```sql
CREATE TABLE messages (            -- idempotent ingest
  msg_id TEXT PRIMARY KEY, received_at REAL NOT NULL,
  device_id TEXT NOT NULL, kind TEXT NOT NULL, payload TEXT NOT NULL);

CREATE TABLE events (
  id INTEGER PRIMARY KEY, device_id TEXT NOT NULL,
  received_at REAL NOT NULL, event TEXT NOT NULL, severity TEXT NOT NULL,
  state TEXT, evidence TEXT,                    -- JSON, incl. conservation terms
  ack_state TEXT DEFAULT 'UNACK');

CREATE TABLE summaries (
  id INTEGER PRIMARY KEY, device_id TEXT NOT NULL, received_at REAL NOT NULL,
  avg_moisture REAL, min_moisture REAL, max_moisture REAL, std_moisture REAL,
  avg_temp REAL, avg_rh REAL, vbat REAL, soc_pct REAL, mAh REAL);

CREATE TABLE energy_log (          -- makes §7 falsifiable
  received_at REAL, device_id TEXT,
  tx_count INT, tx_bytes INT, awake_s REAL, probe_cycles INT);

CREATE TABLE probe_results (       -- conservation audit trail / labelled data
  received_at REAL, device_id TEXT, verdict TEXT,
  V_delivered_ml REAL, V_soil_delta_ml REAL, residual_ratio REAL,
  ratio_vs_reference REAL, flow_calibrated_ppl REAL);
```

---

## 9. Backend & dashboard

**Flask** over **SQLite in WAL mode** — one file, zero ops, survives a demo-day crash, inspectable by a judge with `sqlite3`.

| Route | Purpose |
|---|---|
| `POST /api/ingest` | Idempotent device ingest |
| `GET  /` | Dashboard |
| `GET  /api/events` · `/api/summary` · `/api/energy` · `/api/probes` | Dashboard data |
| `POST /api/ack` | Operator acknowledges an alert → closes the demo loop |
| `GET  /health` | Liveness |

**Dashboard panels:**

1. **Live soil trace + shaded diurnal baseline band** — the most persuasive visual: *the anomaly is a clean departure from the learned envelope.*
2. **Conservation bar chart** — per probe cycle: `V_delivered` vs `V_soil + V_ET + V_residual`, with the residual segment coloured by verdict. The single clearest picture of the core algorithm.
3. **Flow strip chart** — live L/min with the pump state band behind it. A continuous leak is *immediately visible here*.
4. **CUSUM state ladder** — `NORMAL → SUSPECT → ALARM` live, trip threshold marked.
5. **Alert stream with evidence** — every card expands to show *why*, including the conservation terms.
6. **Energy & bandwidth** — readings generated vs. messages sent, mAh/day, estimated life, against the naive baseline.
7. **Scenario launcher** — inject the demo faults server-side.

### 9.1 On-device UI — ST7735 128×160 (landscape 160×128)

A colour TFT is a genuine upgrade over a text display for criterion 5: we can show the **algorithm's arithmetic**, not just its output. Four screens, cycled with the button on GPIO0, LVGL 9 + `LV_DISPLAY_RENDER_MODE_PARTIAL` (DeskBuddy's proven flush pattern, `dmaWait()` before releasing CS).

**① LIVE** *(default)* — the always-on view

```text
┌────────────────────────┐
│ ●OK            ▓▓▓░ 3.9V│   state pill · battery bar
│                        │
│  41.8 %                │   Montserrat 28
│                        │
│  ╱╲__╱╲___╱╲__  flow   │   64-px sparkline + baseline band
│  ────────  ── 0.0 L/m │   shaded band = learned diurnal baseline ±2σ
├────────────────────────┤
│ next tx 04:12   96% ↓   │   countdown · bandwidth saved today
└────────────────────────┘
```

**② CONSERVATION** — *the money screen.* The core algorithm, drawn.

```text
┌────────────────────────┐
│ CONSERVATION     probe 3│
│                        │
│ V_del   ████████ 250 mL│
│ V_soil  ██        38 mL │
│ V_ET    ▌          3 mL │
│ V_resid ██████   209 mL │  ← red: unaccounted
│                        │
│  residual_ratio  0.834  │  Montserrat 28
│  vs reference    0.29 ✗ │  ← LEAK_OR_BYPASS
└────────────────────────┘
```

A stacked bar where the residual segment grows as the leak grows is the single most persuasive visual in the whole project. When the ratio exceeds 1.15 the bar flips green-inverted and reads `SENSOR_DRIFT` — which no judges' guess would produce.

**③ PROBE** — live flow during a controlled-volume cycle

```text
┌────────────────────────┐
│ PUMP ● ON      250 mL  │
│                        │
│  ╭──────────╮          │   LVGL arc = progress to target volume
│  │   4.2    │          │   centre = live L/min (Montserrat 28)
│  ╰──────────╯          │
│  pulses 1247  ·  21 s  │
│  ▼ d(soil)/dt +0.31 %/min
└────────────────────────┘
```

**④ ALERTS** — last 3 with severity colour and a one-line reason, tappable to expand the evidence JSON.

**Power discipline (§6.7 is the mechanism):**

```c
void ui_power_tick(uint32_t state) {
    uint16_t duty;
    if      (state == ST_ALARM || state == ST_PROBE) duty = 255; // 100 %
    else if (millis() - lastInteraction < 30000)      duty = 255; // 100 %
    else if (millis() - lastInteraction < 300000)     duty =  38; //  15 %
    else                                            { duty =   0; fillScreen(TFT_BLACK);
                                                                 tft.writecommand(ST7735_SLPIN); }
    ledcWrite(BL_CH, duty);
    if (duty) tft.writecommand(ST7735_SLPOUT);
}
```

Plus two rules: **mostly-black UI** (white pixels cost real current on RGB565 — a dark theme is both better-looking here and cheaper), and **redraw only on change** — never call `lv_timer_handler()` in a free-running loop like DeskBuddy does, because that pins the CPU awake and defeats deep sleep entirely.

**On-device TFT (criterion 5):** see §9.1 — four screens including the conservation breakdown, so the demo is fully self-contained on the device if the network dies.

---

## 10. Validation plan

This is the section that separates a prototype from a defensible project.

### 10.1 What we can and cannot claim

| Claim | How we earn it |
|---|---|
| Detector works on real soil-moisture dynamics | Replay the Mendeley dataset through `anomaly_core.h` |
| Detector is tuned, not guessed | Fit `K`, `H`, `σ_floor` on the dataset; report held-out metrics |
| **Leak/pump/blockage separation works** | Conservation gate + probe table; demonstrated on hardware (§5.6) |
| **Flow sensor is trustworthy** | Volume-calibration procedure run on the bench, error recorded (§5.7) |
| Energy/bandwidth reduction is real | Measured on the bench (§10.4) |
| False-alarm rate is acceptable | Reported as **false alarms/day** under replay |

**We do not claim** root-cause accuracy for faults the hardware cannot observe — sub-resolution leaks (§14) and multi-emitter localisation.

### 10.2 Anomaly injection protocol

The dataset has no faults, so we inject with a **published, parameterised protocol**, keeping the clean signal for baseline fitting. The flow channel gets **synthetic profiles** derived from the probe controller, since the dataset has no flow channel:

| Injection | Model | Expected label |
|---|---|---|
| Flatline | `x_t = x_{t−1}` for a random span | `SENSOR_STUCK` |
| Rail / disconnect | `x_t = 4095 ± 2 LSB` | `SENSOR_DISCONNECTED` |
| Spike burst | `x_t += 5σ` for 1–3 samples | `SENSOR_SPIKE_BURST` |
| Slow leak | Normal ET decay **extended**; flow continues, moisture suppressed | `LEAK_OR_BYPASS` |
| Severe leak | flow at target, `ΔV_soil ≈ 0` | `LEAK_SEVERE` |
| Continuous leak | flow profile while `pump_state = OFF` | `CONTINUOUS_LEAK` |
| Blockage | pump ON, flow `≈ 0` | `BLOCKAGE_NO_FLOW` |
| Probe drift | `ΔV_soil` scaled `+25 %` vs. delivered volume | `OVER_IRRIGATION` ⇒ `SENSOR_DRIFT` |
| Water stress | Baseline shifted down `3σ`, held | `WATER_STRESS` |
| Flow channel death | pulse count frozen; `flow = 0` | `FLOW_SENSOR_FAULT` |
| DHT11 dropout | `NaN` for k samples | `SENSOR_TIMEOUT` |
| **External event** | step `+8σ`, exponential decay | `EXTERNAL_EVENT` — **must NOT alert** |

The **external-event row is a deliberate negative control**: rain *must not* trigger an irrigation alert. A detector that alarms on rain is not deployable, and reviewers will ask. The **probe-drift row** validates the conservation gate's most distinctive claim (§5.6b).

### 10.3 Metrics

Held-out split: **precision, recall, F1 per class, detection latency (samples & seconds), false alarms/day, conservation-ratio error vs. ground truth, and CPU cost (µs/eval)**. Include a `K`/`H` sensitivity sweep so the thresholds look chosen rather than lucky.

### 10.4 Bench measurement

- **Flow calibration:** run the §5.7 volume procedure twice; record the error. This number goes on a slide.
- **Energy:** USB power meter — record mAh over ≥30 min per policy: `always-on`, `duty-cycled TX`, `AgriGuard Edge`. Sleep current measured with the radio disabled. *(No INA219, so this is the only energy measurement path — budget 30 min for it.)*
- **Backlight A/B** *(required — it is the headline claim)*: measure the same 10-minute window twice, once with `TFT_BL` tied to 3V3 and once with LEDC gating. Put both numbers on the slide. This is a two-minute measurement that produces the most memorable slide in the deck.
- **Display sleep correctness:** confirm in `SLPIN` the node draws <100 µA, not merely "the screen is dark".
- **Bandwidth:** server-side byte counters from `energy_log`.
- **Fairness:** identical dataset window, identical code path, only the TX policy differs.

### 10.5 Reproducibility

`python tools/anomaly_replay --csv data/mendeley.csv --inject leak --flow-profile synthetic --report out/` → one command, one metrics file, same result on any machine. Judges can re-run it. Worth more than any chart.

---

## 11. Demo scenarios

Each triggerable physically and via the dashboard launcher.

| # | Do this physically | Expect | Endpoint |
|---|---|---|---|
| **1** | Baseline for 10 min | `OK`, trace inside baseline band, summaries every 15 min | 96/day |
| **2** | Normal probe cycle — let it reach 250 mL | `IRRIGATION_OK`, `residual_ratio ≈ 0.9` | `probe_result` |
| **3** | **★ Loosen a fitting downstream of the flow meter; let it drip into a bucket.** Pump OFF | Flow > 0 with pump off ⇒ **`CONTINUOUS_LEAK` HIGH** | `alert` |
| **4** | **★ Crack a fitting downstream of the meter; run a probe cycle** | `V_delivered = 250 mL`, `ΔV_soil` collapses ⇒ **`LEAK_SEVERE` HIGH** | `alert` |
| **5** | **Kink the hose downstream of the meter; run a probe cycle** | Pump ON, flow `≈ 0` ⇒ **`BLOCKAGE_NO_FLOW` HIGH** *(note: a downstream kink reads as blockage, **not** leak)* | `alert` |
| **6** | Unplug the soil probe | `SENSOR_DISCONNECTED` within one health-gate pass | `alert` |
| **7** | Hold the probe steady / short to a rail | `SENSOR_STUCK` after `K=8` evals | `alert` |
| **8** | **Stop irrigating for ~2 h** | Deficit grows vs. diurnal envelope ⇒ **`WATER_STRESS`** | `alert` |
| **9** | Simulate rain (pour water on the probe) | `EXTERNAL_EVENT` INFO, **no alert** — negative control | `summary` |
| **10** | Read the dashboard after any of the above | ~96 % messages saved; conservation bars explain each verdict | — |

> **Hydraulics warning — the mistake that would have embarrassed us.** With the meter downstream of the pump, a kink *after* the sensor removes flow from the reading, so it reports **blockage**, not a leak. A leak requires water to leave the system **after** the meter. Scenarios 3–5 are ordered to make that distinction explicit, and it is worth *saying out loud* during the demo — it demonstrates we understand the plumbing rather than just the code.
>
> **Defensive ordering:** 1 → 2 → 6 → 8 → 9 → 3 → 4 → 5. Easy deterministic ones first (so a failure has a fallback), physical plumbing manipulations last.

---

## 12. 24-hour execution plan

### 12.1 MoSCoW scope

| Priority | Item | Degraded mode |
|---|---|---|
| **Must** | Hardware bring-up: flow, soil, DHT11, ST7735, relay + pump | — |
| **Must** | Flow level shifting + PCNT capture + volume calibration | poll flow at 1 Hz instead of PCNT |
| **Must** | `anomaly_core.h` stages ①–④ | — |
| **Must** | **Conservation gate §5.6** | report `residual_ratio` as an unclassified number |
| **Must** | Flask ingest + SQLite + alerts view | serial console instead of dashboard |
| **Must** | Dataset replay + metrics table | hand-computed on 50 rows |
| **Must** | Energy/bandwidth measurement | modelled numbers, flagged unverified |
| **Should** | `CONTINUOUS_LEAK` watch (§5.7) | probe cycles only |
| **Should** | Probe matrix + calibration UI | — |
| **Should** | ST7735 UI: LIVE + CONSERVATION screens | LIVE screen only; dashboard-only |
| **Should** | 4 demo scenarios | 2 scenarios |
| **Could** | Adaptive summary cadence (→ 99 %) | fixed 15-min summaries (→ 96 %) |
| **Could** | Embedded depth-2 decision tree | CUSUM only |
| **Could** | Second flow meter for zone localisation | single-zone attribution |
| **Won't** | INA219 / current sensing — **dropped**, flow sensor covers it | — |
| **Won't** | SD logging, OTA, LoRa, mobile app | — |

### 12.2 Hourly plan

| Hours | Track A — firmware | Track B — backend | Track C — data/model | Track D — deck/demo |
|---|---|---|---|---|
| **0–2** | Lock pin map; **level-shift and verify the flow sensor first**; bring up each sensor one at a time | Flask skeleton + schema | Download dataset, inspect schema, confirm channels | Problem framing + rubric mapping |
| **2–4** | PCNT capture; **run the flow volume-calibration**; power-gating; ADC loop | `POST /api/ingest` + idempotency | Build replay harness skeleton | — |
| **4–6** | Stages ①–② on hardware | Dashboard: live trace + flow strip | Fit baseline on dataset | **⚠ KILL CHECK 1 (H+4):** flow + pump stable? If not → bench-supply fallback, poll instead of PCNT |
| **6–9** | Stages ③–④ + state machine | Events view + evidence JSON | Fit `K`,`H`; run injections | Draft architecture slide |
| **9–12** | **Conservation gate + controlled-volume probe** | Conservation bar chart | **⚠ KILL CHECK 2 (H+8):** conservation numbers stable across cycles? If not → ship it as an unclassified ratio + continuous-leak rule | Demo script v1 |
| **12–15** | `CONTINUOUS_LEAK`; ST7735 CONSERVATION screen; WiFi TX scheduler | Scenario launcher + energy panel | Held-out validation, sensitivity sweep | **⚠ KILL CHECK 3 (H+12):** end-to-end alert on hardware? If not → serial-bridge fallback |
| **15–18** | Integrate + soak test | Polish + failure states | Probe-drift validation row | Record backup video of every scenario |
| **18–20** | Bug bash; **freeze features** | Freeze | Freeze docs | Rehearse defense ×3; timed 4-min pitch |
| **20–24** | Buffer for breakage only | Buffer | — | Submission, README, demo re-run |

### 12.3 Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| **5 V flow output destroys a GPIO** | **HIGH** | §6.2 divider or AHCT125 **before** first power-up; this is the first task at H+0 |
| **TFT backlight tied to 3V3** | **HIGH** | `-DTFT_BL=9` + LEDC PWM (§6.6). Wired conventionally it costs 1 699 mAh/day and makes every power claim a lie |
| SPI at 40 MHz on breadboard jumpers | MED | Dropped to 20 MHz (§6.6); artefacts otherwise look like UI bugs |
| `dmaWait()` missed before deep sleep | MED | §6.7 trap 1 — torn frames or crash on wake |
| Flow sensor never reads a pulse (wiring/fault) | **HIGH** | Verify at H+1 with a hand-pumped 500 mL before anything else depends on it |
| **Sub-1 L/min leaks invisible** (§14) | MED | Demo uses leaks ≥ 1 L/min; documented as a hardware limit, not a design flaw |
| 12 V pump needs a real 12 V adapter | **HIGH** | Adapter sized ≥ stall current; relay COM/NO on 12 V+, common GND; node stays on USB/buck |
| Flow meter reads low when horizontal | MED | Mount vertical, arrow up; verify against the 500 mL calibration |
| Pump inrush brownouts ESP32 | MED | 1000 µF cap, separate rail; looks identical to a crash otherwise |
| Dataset schema differs from catalog page | MED | Verify at H+0–1; fallback = synthetic diurnal model, documented |
| Breadboard contact bounce | MED | Short jumper runs, secure connections, verify at H+2 not H+20 |
| Devkit power LED burns ~mA | MED | Remove LED or deselect the rail |
| Venue WiFi fails | MED | ST7735 demo works standalone; AP mode / hotspot fallback |
| Scope overrun | **HIGH** | MoSCoW table + kill checks; the `Could` column is what we sacrifice |

---

## 13. Defense script

**Opening (30 s).** "Every reading to the cloud works until you count the power. A node that samples every 30 seconds with everything switched on survives about a day on a small cell — ours is mains-powered for the demo and battery-ready by design — because it sends decisions, not data, and it can show you the arithmetic behind every decision."

**Problem (45 s).** Farmers can't afford cloud round-trips per reading and can't afford a node that dies. Worse, the two most expensive faults — a silent leak and a drifting probe — are *invisible* to moisture monitoring alone.

**Approach (60 s).** Four stages: health-gate the sensors, build a robust diurnal baseline, run EWMA-CUSUM on the residuals, confirm with N-of-M persistence. Then the core: the node pulses its own pump to a **fixed known volume** and checks mass conservation. Water delivered but not retained is a leak. And if more water appears in the soil than was ever delivered — physically impossible — **the probe is lying**, and we've caught sensor drift with no labels and no reference data.

**Evidence (60 s).** Mendeley replay with published fault injections: precision, recall, F1, latency, false alarms/day, a rain negative control, and a probe-drift case. Same C++ as the firmware. Flow sensor calibrated against 500 mL of measured water, error on screen.

**Impact (45 s).** ~96 % fewer messages. 2 256 → 229 mAh/day — and I'll tell you honestly that 203 of those 229 are *pumping water*, so duty cycle, not compute, is the real constraint. And the conservation gate labels its own sensor faults, so the detector improves every cycle.

**Likely challenges — prepared answers:**

| Challenge | Response |
|---|---|
| "What if the leak is slower than your meter can see?" | Correct — below ~1 L/min the ZJ-S201 is blind. It's a hardware limit, stated in §14, and it's why the meter sits *before* the leak point so we integrate over long windows. A clamp-on ultrasonic or pressure sensor is the real fix. |
| "Rules aren't ML." | The tree is real but small by design; the contribution is the conditioning plus the conservation gate, and the whole thing fits in <20 kB RAM. |
| "Your dataset has no anomalies or flow." | Correct. So we inject with a published protocol, and the conservation law generates sensor-fault labels the dataset can't provide. |
| "How do you know V_wetted?" | We don't, and we don't need to — one reference cycle calibrates the ratio. That's stated in §5.6a. |
| "99 % reduction?" | We measure ~96 % and report it. 99 % needs adaptive summaries, which is a `Could`, not a claim. |
| "Does it run on battery?" | Demo is mains-powered (12 V + USB) by design; we report measured supply current per policy from a USB meter, plus the backlight A/B. The §7 table is the battery-variant reference. |
| "Resistive probes corrode — won't yours drift?" | Yes, over weeks, and that is exactly what the conservation gate is for: impossible mass balance ⇒ `SENSOR_DRIFT`, no labels needed. Gating stretches electrodes to a season. |
| "ZJ-S201 says 450 p/L — trusted?" | No. Nominal only; the 500 mL volume calibration overwrites it, and every probe cycle re-validates against the reference ratio. |
| "Why isn't the screen on the whole time?" | Deliberately. The backlight is the biggest single load in the node — 1 699 mAh/day wired the conventional way. It wakes on alarm, probe, or a button press. |
| "What about multi-zone farms?" | One meter gives one zone. Attribution to a specific emitter needs a second meter — listed as a `Could`, not claimed. |

---

## 14. Appendix

### 14.1 Success criteria (measurable, by H+20)

```text
[ ] Flow sensor verified against 500 mL; error recorded
[ ] All sensors + ST7735 + relay + pump demonstrated on the bench
[ ] anomaly_core.h compiles for ESP32 AND host from the same source
[ ] ≥ 8 of 10 demo scenarios pass live
[ ] Detection: F1 ≥ 0.85 on held-out replay; 0 false alerts on rain
[ ] Detection latency ≤ 120 s for a sustained 3σ shift
[ ] Conservation ratio reproducible within ±10 % across 3 consecutive cycles
[ ] Messages/day ≤ 120; bandwidth reduction ≥ 90 % measured
[ ] Energy ≤ 260 mAh/day measured; ≥ 80 % reduction vs. naive baseline
[ ] Every alert shows its conservation terms as evidence
[ ] 4-min pitch rehearsed; backup video recorded
```

### 14.2 Honest limitations

1. **Sub-1 L/min leaks are invisible to the rate meter.** The ZJ-S201's mechanical floor is ~1 L/min. A slow weep still twitches the turbine, so the firmware integrates pump-OFF pulses over a 15-min window (≥20 pulses ⇒ `MICRO_LEAK`, MED) — volume-accurate even where rate is meaningless. Truly motionless water below that remains invisible; this needs a different sensor to truly fix.
2. **One meter = one zone.** No multi-emitter localisation; a leak is attributed to *this* zone, not to a specific emitter.
3. **Flow accuracy is unquoted by the vendor**, so conservation ratios inherit that uncertainty — the 500 mL calibration IS the datasheet. Ratios near 1.0 are within noise; only ratios far from 1.0 are actionable. Stated rather than hidden.
4. **The dataset has no fault labels and no flow channel.** All fault metrics come from our own injection protocol; all flow behaviour is hardware-measured or synthetically profiled.
5. **`V_runoff ≈ 0` is an assumption** during short pulses. Valid for a 250 mL pulse; not valid for a full flood irrigation. Documented in §5.6.
6. **Breadboard prototype.** Contact bounce and thermal drift are real; a PCB is future work.
7. **Pump dominates energy.** In the battery variant, firmware optimisation extends node life ~79×, but the binding constraint remains irrigation duty cycle. The demo build is mains-powered, so we report measured current per policy instead of days-per-charge.
8. **DHT11 is weak** (±2 °C, ±20 % RH). Contextual only, never a primary gate.
9. **Resistive probe corrodes.** DC through wet soil eats electrodes; gating (200 ms/sample) stretches this to a season, and the resulting slow drift is exactly what §5.6b detects as `SENSOR_DRIFT`. AC excitation is future work.
10. **Bone-dry air can pin the ADC near rail** and read as `SENSOR_DISCONNECTED` — which is honest: a probe reading full-scale dry is out of the soil.
11. **No battery in the demo BOM.** Off-grid life numbers (§7) are design reference; the demo reports measured supply current + 96 % bandwidth.

### 14.3 Glossary

| Term | Meaning |
|---|---|
| **V_residual** | `V_delivered − ΔV_soil − V_ET`. Unaccounted water. The leak measure. |
| **residual_ratio** | `V_residual / V_delivered`. Banded into verdicts in §5.6. |
| **MAD** | Median Absolute Deviation — outlier-robust spread estimator |
| **CUSUM** | Cumulative Sum — accumulates small sustained deviations; detects *shifts*, not spikes |
| **EWMA** | Exponentially Weighted Moving Average — the adaptive baseline |
| **Hour-of-day bucket** | Per-hour baseline slot, so 08:00 learns what 08:00 looks like |
| **Guarded adaptation** | Baseline updates only from healthy samples, so faults can't be absorbed |
| **Hysteresis** | Separate promote/demote thresholds, preventing alert flapping |
| **PCNT** | ESP32 hardware pulse counter — counts flow pulses without CPU load, even in deep sleep |
| **pulses/L** | ZJ-S201 calibration constant (nominal 450, F = 7.5 × Q), verified by volume at runtime |
| **Backlight PWM** | LEDC-controlled dimming on GPIO9 — the largest single power lever in the UI (§7) |
| **Label-free supervision** | Physical conservation laws yielding labelled faults without ground truth |

---

*Sources: dataset [Mendeley `65jxyrxv7b`](https://data.mendeley.com/datasets/65jxyrxv7b) (La Trobe University, 2024, CC BY 4.0) via [IoTDataset catalog](https://iotdataset.com/data/smart-agriculture-plant-health-iot-sensors). Earlier drafts retained at `docs/_archive/`.*