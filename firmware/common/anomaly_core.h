/**
 * AgriGuard Edge — firmware/common/anomaly_core.h
 *
 * THE entire edge detector. Pure C/C++, no Arduino, no WiFi, no dynamic
 * allocation beyond a caller-supplied struct. Compiles for ESP32-S3
 * (via AgriGuardEdge_HardwarePIO) AND for the host replay harness
 * (tools/anomaly_replay/cpp_replay.cpp) from this single file.
 *
 * Pipeline (§5 of docs/architechture.md):
 *   ① Sensor Health Gate (incl. flow-channel checks)
 *   ② Robust Diurnal Baseline + Residuals (24 hour-of-day buckets)
 *   ③ EWMA-CUSUM Shift Detection
 *   ④ Persistence / Hysteresis State Machine
 *   ⑤ Flow-Mass-Conservation Gate  ★ technical core
 *   ⑥ Controlled-Volume Probe + Continuous-Leak Watch
 *
 * Conventions:
 *  - All state lives in ag_core_t, owned by the caller (static / PSRAM).
 *  - All functions are `static inline` so the header is the implementation.
 *  - Time is seconds (monotonic). Hour-of-day bucket = (epoch_sec/3600)%24
 *    when wall time is known, else derived from uptime.
 *  - Moisture is calibrated % (0..100). Raw ADC only enters stage ①.
 *  - Flow rate is L/min, volume is mL.
 */
#ifndef AGRIGUARD_ANOMALY_CORE_H
#define AGRIGUARD_ANOMALY_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Tuning constants (K/H/sigmas are FIT via replay, §10.3 — these are  */
/* the defaults the sweep starts from, not guesses shipped as truth)  */
/* ------------------------------------------------------------------ */
#define AG_PULSES_PER_LITRE_NOMINAL 5888.0f
#define AG_FLOW_ALPHA               0.25f
#define AG_FLOW_RATE_MAX_LPM        35.0f
#define AG_FLOW_RATE_MIN_TRUST_LPM  1.0f   /* YF-S201 mechanical floor (§14.1) */
#define AG_LEAK_EPS_LPM             0.30f  /* pump-OFF flow above this = leak */
#define AG_BASELINE_ALPHA           0.05f
#define AG_SIGMA_FLOOR              0.40f  /* % moisture, guards /0 on flat win */
#define AG_SOFT_BAND_SIGMA          2.0f   /* guarded adaptation gate (§5.3) */
#define AG_CUSUM_K                  0.5f
#define AG_CUSUM_H                  5.0f
#define AG_SUSPECT_Z                2.0f
#define AG_ALARM_Z                  3.0f
#define AG_PERSIST_M                5
#define AG_PERSIST_N                3
#define AG_PERSIST_MIN_S            90
#define AG_RECOVERY_CLEAN           3
#define AG_STUCK_K                  8
#define AG_STUCK_MAD_PCT            0.15f
#define AG_SPIKE_MAD_MULT           6.0f
#define AG_VTARGET_ML               250.0f
#define AG_T_OBSERVE_S              180
#define AG_BATT_LOW_V               3.40f
#define AG_BASELINE_WIN             16     /* robust window for med/MAD */

/* Conservation verdict bands on residual_ratio = V_resid / V_del (§5.6) */
#define AG_R_OK_LO   0.80f
#define AG_R_OK_HI   1.15f
#define AG_R_PART_LO 0.45f
#define AG_R_LEAK_LO 0.05f

/* ------------------------------------------------------------------ */
/* Taxonomy (§5.8)                                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    AG_EV_NONE = 0,
    AG_EV_IRRIGATION_OK,
    AG_EV_CONTINUOUS_LEAK,
    AG_EV_LEAK_SEVERE,
    AG_EV_LEAK_OR_BYPASS,
    AG_EV_OVER_IRRIGATION,
    AG_EV_BLOCKAGE_NO_FLOW,
    AG_EV_BLOCKAGE_PARTIAL,
    AG_EV_WATER_STRESS,
    AG_EV_SENSOR_STUCK,
    AG_EV_SENSOR_OUT_OF_RANGE,
    AG_EV_SENSOR_DISCONNECTED,
    AG_EV_SENSOR_DRIFT,
    AG_EV_SENSOR_SPIKE_BURST,
    AG_EV_SENSOR_TIMEOUT,
    AG_EV_FLOW_SENSOR_FAULT,
    AG_EV_FLOW_NO_PULSE,
    AG_EV_FLOW_IMPLAUSIBLE,
    AG_EV_FLOW_NOISY,
    AG_EV_EXTERNAL_EVENT,
    AG_EV_POWER_LOW
} ag_event_t;

typedef enum { AG_SEV_NONE = 0, AG_SEV_INFO, AG_SEV_LOW, AG_SEV_MED, AG_SEV_HIGH } ag_sev_t;
typedef enum { AG_ST_NORMAL = 0, AG_ST_SUSPECT, AG_ST_ALARM } ag_state_t;

/* ------------------------------------------------------------------ */
/* Sample in / result out                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float    moisture_pct;   /* calibrated soil % */
    int      adc_raw;        /* raw 12-bit, -1 if unavailable on host */
    float    temp_c;         /* DHT11, NAN if missing */
    float    rh_pct;         /* DHT11, NAN if missing */
    float    flow_lpm;       /* smoothed flow rate */
    float    vbat_v;         /* cell voltage */
    uint8_t  pump_on;        /* commanded pump state (known by construction) */
    uint32_t t_sec;          /* monotonic seconds */
    uint8_t  hour;           /* 0..23 hour-of-day bucket */
} ag_sample_t;

typedef struct {
    ag_event_t event;
    ag_sev_t   severity;
    ag_state_t state;
    /* evidence (§8 — every alert must carry its reasoning) */
    float z_soil;
    float cusum_up, cusum_down;
    float baseline_moist, sigma;
    float deficit;
    float v_delivered_ml, v_soil_delta_ml, v_et_ml;
    float residual_ratio, ratio_vs_ref;
    float flow_lpm;
    uint8_t degraded;        /* 1 if conservation gate unavailable */
} ag_result_t;

/* ------------------------------------------------------------------ */
/* Core state (caller-owned; ~1.5 KB, fine for internal RAM)           */
/* ------------------------------------------------------------------ */
typedef struct {
    /* ② baseline: 24 hour-of-day buckets */
    float moist_base[24];
    float rh_base[24];
    float et_rate[24];       /* expected drying %/h, learned in NORMAL */
    float sigma_base[24];
    uint8_t base_init[24];

    /* robust window for median/MAD */
    float win[AG_BASELINE_WIN];
    float win_d[AG_BASELINE_WIN]; /* |Δx| innovation window */
    uint8_t win_n;
    float prev_moist;
    uint8_t prev_valid;

    /* ③ CUSUM */
    float cusum_up, cusum_down;
    float ewma_z;

    /* ④ persistence */
    ag_state_t state;
    uint8_t hist[AG_PERSIST_M]; /* 1 = anomalous eval */
    uint8_t hist_n;
    uint32_t suspect_since;
    uint32_t state_age_s;
    uint8_t clean_run;

    /* ⑤ conservation reference */
    float r_ref;            /* ΔV_soil/V_del on known-good cycle, 0 = uncal */
    float pulses_per_litre; /* runtime-calibrated (§5.7) */

    /* ⑥ probe */
    uint8_t probe_active;
    float probe_v_ml;
    float probe_moist_start;
    uint32_t probe_t0;
    float last_verdict_ratio;

    /* flow health */
    uint32_t flow_stale_s;  /* seconds with pump ON but ~0 flow */
    uint8_t  leak_windows;  /* consecutive pump-OFF windows with flow */
    uint32_t last_t;

    /* stats */
    uint32_t evals;
} ag_core_t;

typedef struct {
    float cusum_k, cusum_h;
    float suspect_z, alarm_z;
    float sigma_floor;
    float v_wetted_ml;      /* mL of soil water per 1 % moisture — field cal */
} ag_config_t;

/* ------------------------------------------------------------------ */
/* Small robust helpers (no allocation, insertion sort on ≤16 elems)   */
/* ------------------------------------------------------------------ */
static inline void ag_sort16(float *a, int n) {
    for (int i = 1; i < n; i++) {
        float k = a[i]; int j = i - 1;
        while (j >= 0 && a[j] > k) { a[j + 1] = a[j]; j--; }
        a[j + 1] = k;
    }
}

static inline float ag_median(const float *x, int n, float *tmp) {
    if (n <= 0) return 0.f;
    for (int i = 0; i < n; i++) tmp[i] = x[i];
    ag_sort16(tmp, n);
    if (n & 1) return tmp[n / 2];
    return 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

static inline float ag_mad_sigma(const float *x, int n, float *tmp) {
    if (n <= 0) return AG_SIGMA_FLOOR;
    float med = ag_median(x, n, tmp);
    float dev[AG_BASELINE_WIN];
    for (int i = 0; i < n; i++) dev[i] = fabsf(x[i] - med);
    float mad = ag_median(dev, n, tmp);
    float s = 1.4826f * mad;
    return (s < AG_SIGMA_FLOOR) ? AG_SIGMA_FLOOR : s;
}

static inline float ag_fmax0(float v) { return v > 0.f ? v : 0.f; }

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */
static inline ag_config_t ag_config_default(void) {
    ag_config_t c;
    c.cusum_k = AG_CUSUM_K; c.cusum_h = AG_CUSUM_H;
    c.suspect_z = AG_SUSPECT_Z; c.alarm_z = AG_ALARM_Z;
    c.sigma_floor = AG_SIGMA_FLOOR;
    c.v_wetted_ml = 40.0f; /* 1 % moisture ≈ 40 mL in the demo pot; field-cal */
    return c;
}

static inline void ag_init(ag_core_t *c) {
    for (int i = 0; i < 24; i++) {
        c->moist_base[i] = 0.f; c->rh_base[i] = 0.f;
        c->et_rate[i] = 0.15f;  /* mild drying default %/h */
        c->sigma_base[i] = 1.0f; c->base_init[i] = 0;
    }
    for (int i = 0; i < AG_BASELINE_WIN; i++) { c->win[i] = 0.f; c->win_d[i] = 0.f; }
    c->win_n = 0; c->prev_valid = 0; c->prev_moist = 0.f;
    c->cusum_up = 0.f; c->cusum_down = 0.f; c->ewma_z = 0.f;
    c->state = AG_ST_NORMAL;
    for (int i = 0; i < AG_PERSIST_M; i++) c->hist[i] = 0;
    c->hist_n = 0; c->suspect_since = 0; c->state_age_s = 0; c->clean_run = 0;
    c->r_ref = 0.f;
    c->pulses_per_litre = AG_PULSES_PER_LITRE_NOMINAL;
    c->probe_active = 0; c->probe_v_ml = 0.f;
    c->probe_moist_start = 0.f; c->probe_t0 = 0; c->last_verdict_ratio = 0.f;
    c->flow_stale_s = 0; c->leak_windows = 0; c->last_t = 0;
    c->evals = 0;
}

/* ------------------------------------------------------------------ */
/* ① Sensor Health Gate — never let untrusted data reach analytics     */
/* Returns AG_EV_NONE when healthy, else the fault code.               */
/* ------------------------------------------------------------------ */
static inline ag_event_t ag_health_gate(ag_core_t *c, const ag_sample_t *s,
                                        float *innov_out) {
    /* Soil out-of-range (calibrated %) */
    if (!(s->moisture_pct >= 2.0f && s->moisture_pct <= 98.0f))
        return AG_EV_SENSOR_OUT_OF_RANGE;
    /* Rail / disconnect (raw ADC pinned) */
    if (s->adc_raw >= 0 && (s->adc_raw <= 2 || s->adc_raw >= 4093))
        return AG_EV_SENSOR_DISCONNECTED;
    /* Innovation features from robust window */
    if (c->prev_valid) {
        float dx = fabsf(s->moisture_pct - c->prev_moist);
        if (innov_out) *innov_out = dx;
        if (c->win_n >= AG_STUCK_K) {
            float tmp[AG_BASELINE_WIN];
            float mad = 0.f;
            /* MAD of the innovation window */
            float med = ag_median(c->win_d, c->win_n, tmp);
            float dev[AG_BASELINE_WIN];
            for (int i = 0; i < c->win_n; i++) dev[i] = fabsf(c->win_d[i] - med);
            mad = ag_median(dev, c->win_n, tmp);
            /* flatline: near-zero innovation sustained */
            if (mad * 1.4826f < AG_STUCK_MAD_PCT && med < AG_STUCK_MAD_PCT)
                return AG_EV_SENSOR_STUCK;
            /* spike burst: single huge jump vs robust spread */
            float spread = 1.4826f * mad;
            if (spread < AG_SIGMA_FLOOR) spread = AG_SIGMA_FLOOR;
            if (dx > AG_SPIKE_MAD_MULT * spread && dx > 3.0f)
                return AG_EV_SENSOR_SPIKE_BURST;
        }
    }
    /* DHT11 timeout — corroboration only, never a primary gate by itself,
       but a NaN on BOTH channels for repeated evals is a timeout. */
    if ((isnan(s->temp_c) || isnan(s->rh_pct)) && c->evals > 0) {
        /* counted by caller; single NaN is tolerated (returned as timeout
           only so the estiver excludes it from the RH baseline) */
        return AG_EV_SENSOR_TIMEOUT;
    }
    /* Flow channel health */
    if (s->flow_lpm < -0.05f || s->flow_lpm > AG_FLOW_RATE_MAX_LPM)
        return AG_EV_FLOW_IMPLAUSIBLE;
    /* Battery */
    if (s->vbat_v > 0.1f && s->vbat_v < AG_BATT_LOW_V)
        return AG_EV_POWER_LOW;
    return AG_EV_NONE;
}

/* Push a healthy sample into the robust windows (called AFTER the gate). */
static inline void ag_window_push(ag_core_t *c, float moist) {
    if (c->prev_valid) {
        float dx = fabsf(moist - c->prev_moist);
        if (c->win_n < AG_BASELINE_WIN) {
            c->win[c->win_n] = moist;
            c->win_d[c->win_n] = dx;
            c->win_n++;
        } else {
            for (int i = 0; i < AG_BASELINE_WIN - 1; i++) {
                c->win[i] = c->win[i + 1]; c->win_d[i] = c->win_d[i + 1];
            }
            c->win[AG_BASELINE_WIN - 1] = moist;
            c->win_d[AG_BASELINE_WIN - 1] = dx;
        }
    } else {
        if (c->win_n < AG_BASELINE_WIN) c->win[c->win_n++] = moist;
    }
    c->prev_moist = moist; c->prev_valid = 1;
}

/* ------------------------------------------------------------------ */
/* ⑤ Conservation verdict from the RETENTION ratio (§5.6)               */
/*                                                                     */
/* NOTE on the arch text: §5.6's table is headed "V_residual/V_del"    */
/* but every number in it (and the §8 example JSON: V_del=250,         */
/* V_soil=38.2, V_et=3.1, residual_ratio=0.834 ⇒ LEAK_OR_BYPASS) only  */
/* makes sense as a RETENTION ratio R = (V_soil + V_ET) / V_del:       */
/* a cycle that retains 83 % of the water is OK; one that retains      */
/* 16 % is a leak. A residual (unaccounted) fraction near 1.0 on a     */
/* good cycle would be nonsense. So the verdict bands below apply to   */
/* R, while the reported `residual_ratio` field keeps the §8 wire      */
/* meaning V_resid / V_del (R = 1 − residual_ratio, ET included).      */
/* ------------------------------------------------------------------ */
static inline ag_event_t ag_conservation_verdict(float retention, float v_del_ml) {
    if (v_del_ml <= 1.0f) return AG_EV_NONE; /* no water moved: not a probe */
    if (retention > AG_R_OK_HI)  return AG_EV_OVER_IRRIGATION; /* impossible ⇒ drift */
    if (retention >= AG_R_OK_LO) return AG_EV_IRRIGATION_OK;
    if (retention >= AG_R_PART_LO) return AG_EV_BLOCKAGE_PARTIAL;
    if (retention >= AG_R_LEAK_LO) return AG_EV_LEAK_OR_BYPASS;
    return AG_EV_LEAK_SEVERE;
}

/* Evaluate one controlled-volume probe result. Sets r_ref on the first
   known-good cycle (caller passes is_reference=1 after bench validation). */
static inline ag_event_t ag_probe_evaluate(ag_core_t *c, const ag_config_t *cfg,
                                           float v_del_ml, float d_moist_pct,
                                           float v_et_ml, uint8_t is_reference,
                                           ag_result_t *r) {
    float v_soil = d_moist_pct * cfg->v_wetted_ml;
    if (v_soil < 0.f) v_soil = 0.f;
    float v_resid = v_del_ml - v_soil - v_et_ml;
    float residual = (v_del_ml > 1.f) ? v_resid / v_del_ml : 0.f;
    float retention = (v_del_ml > 1.f) ? (v_soil + v_et_ml) / v_del_ml : 0.f;
    r->v_delivered_ml = v_del_ml;
    r->v_soil_delta_ml = v_soil;
    r->v_et_ml = v_et_ml;
    r->residual_ratio = residual;   /* wire meaning per §8 example JSON */
    if (is_reference && v_del_ml > 1.f) {
        c->r_ref = retention;
        r->ratio_vs_ref = 1.0f;
        c->last_verdict_ratio = residual;
        return AG_EV_IRRIGATION_OK;
    }
    /* Self-calibrating comparison: fraction of the reference retention. */
    if (c->r_ref > 0.001f && v_del_ml > 1.f)
        r->ratio_vs_ref = retention / c->r_ref;
    else
        r->ratio_vs_ref = 1.0f;
    c->last_verdict_ratio = residual;
    ag_event_t v = ag_conservation_verdict(retention, v_del_ml);
    /* ratio > 1.15 with real delivery is physically impossible ⇒ probe drift */
    if (v == AG_EV_OVER_IRRIGATION) return AG_EV_SENSOR_DRIFT;
    return v;
}

/* ------------------------------------------------------------------ */
/* Main per-evaluation step (②③④⑥). Caller runs the health gate first */
/* for fault isolation, then calls this for distribution analytics.    */
/* dt_s = seconds since last eval (60 NORMAL, 10 SUSPECT, 5 ALARM).    */
/* ------------------------------------------------------------------ */
static inline ag_result_t ag_step(ag_core_t *c, const ag_config_t *cfg,
                                  const ag_sample_t *s, float dt_s) {
    ag_result_t r;
    r.event = AG_EV_NONE; r.severity = AG_SEV_NONE; r.state = c->state;
    r.z_soil = 0.f; r.cusum_up = c->cusum_up; r.cusum_down = c->cusum_down;
    r.baseline_moist = 0.f; r.sigma = 1.f; r.deficit = 0.f;
    r.v_delivered_ml = 0.f; r.v_soil_delta_ml = 0.f; r.v_et_ml = 0.f;
    r.residual_ratio = 0.f; r.ratio_vs_ref = 1.f;
    r.flow_lpm = s->flow_lpm; r.degraded = 0;

    uint8_t h = s->hour % 24;
    c->evals++;

    /* ---- ⑥ continuous-leak watch: pump OFF but water moving ★ ---- */
    if (!s->pump_on && s->flow_lpm > AG_LEAK_EPS_LPM) {
        c->leak_windows++;
        if (c->leak_windows >= 2) {
            r.event = AG_EV_CONTINUOUS_LEAK; r.severity = AG_SEV_HIGH;
            r.state = AG_ST_ALARM; c->state = AG_ST_ALARM;
            return r;
        }
    } else if (!s->pump_on) {
        c->leak_windows = 0;
    }
    /* ---- ⑥ blockage: pump ON but no flow ---- */
    if (s->pump_on && s->flow_lpm < 0.2f) {
        c->flow_stale_s += (uint32_t)dt_s;
        if (c->flow_stale_s >= 15) {
            r.event = AG_EV_BLOCKAGE_NO_FLOW; r.severity = AG_SEV_HIGH;
            r.state = AG_ST_ALARM; c->state = AG_ST_ALARM;
            return r;
        }
    } else {
        c->flow_stale_s = 0;
    }

    /* ---- ② baseline ---- */
    if (!c->base_init[h]) {
        c->moist_base[h] = s->moisture_pct;
        c->sigma_base[h] = 1.0f;
        c->base_init[h] = 1;
    }
    float base = c->moist_base[h];
    float sigma = c->sigma_base[h];
    if (sigma < cfg->sigma_floor) sigma = cfg->sigma_floor;
    float resid = s->moisture_pct - base;
    float z = resid / sigma;
    r.z_soil = z; r.baseline_moist = base; r.sigma = sigma;
    r.deficit = base - s->moisture_pct; /* positive = drier than expected */

    /* ---- ③ EWMA-CUSUM on the residual ---- */
    c->ewma_z += 0.2f * (z - c->ewma_z);
    c->cusum_up = ag_fmax0(c->cusum_up + (z - cfg->cusum_k));
    c->cusum_down = ag_fmax0(c->cusum_down + (-z - cfg->cusum_k));
    r.cusum_up = c->cusum_up; r.cusum_down = c->cusum_down;

    uint8_t anomalous = (fabsf(z) > cfg->suspect_z ||
                         c->cusum_up > cfg->cusum_h ||
                         c->cusum_down > cfg->cusum_h) ? 1 : 0;

    /* Large positive step with fast decay signature ⇒ external event
       (rain / third-party irrigation): INFO, must NOT alert. We flag it
       here so the persistence machine below ignores it. */
    uint8_t external = (z > 4.0f) ? 1 : 0;
    if (external) {
        r.event = AG_EV_EXTERNAL_EVENT; r.severity = AG_SEV_INFO;
        /* still adapt slowly — rain is real water, baseline should absorb */
        c->moist_base[h] += 0.5f * AG_BASELINE_ALPHA * (s->moisture_pct - base);
        if (!s->pump_on) c->leak_windows = 0;
        /* do not feed CUSUM/persistence with rain */
        c->cusum_up = 0.f; c->cusum_down = 0.f;
        ag_window_push(c, s->moisture_pct);
        c->last_t = s->t_sec;
        r.state = c->state;
        return r;
    }

    /* ---- ② guarded adaptation: only healthy, in-band samples teach ---- */
    if (!anomalous && fabsf(z) < AG_SOFT_BAND_SIGMA) {
        c->moist_base[h] += AG_BASELINE_ALPHA * (s->moisture_pct - base);
        /* track robust spread from the window */
        if (c->win_n >= 4) {
            float tmp[AG_BASELINE_WIN];
            float wmed = ag_median(c->win, c->win_n, tmp);
            (void)wmed;
            float ws = ag_mad_sigma(c->win, c->win_n, tmp);
            c->sigma_base[h] += AG_BASELINE_ALPHA * (ws - c->sigma_base[h]);
            if (c->sigma_base[h] < cfg->sigma_floor)
                c->sigma_base[h] = cfg->sigma_floor;
        }
        /* learn ET drying rate from NORMAL drying drift (%/h) */
        if (dt_s > 0.f && dt_s < 900.f) {
            float drift = (c->prev_valid)
                ? (s->moisture_pct - c->prev_moist) * 3600.f / dt_s : 0.f;
            if (drift < 0.f && drift > -5.f)
                c->et_rate[h] += AG_BASELINE_ALPHA * ((-drift) - c->et_rate[h]);
        }
        c->clean_run++;
    } else {
        c->clean_run = 0;
    }
    ag_window_push(c, s->moisture_pct);

    /* ---- ④ persistence / hysteresis ---- */
    /* shift history */
    if (c->hist_n < AG_PERSIST_M) {
        c->hist[c->hist_n++] = anomalous;
    } else {
        for (int i = 0; i < AG_PERSIST_M - 1; i++) c->hist[i] = c->hist[i + 1];
        c->hist[AG_PERSIST_M - 1] = anomalous;
    }
    int votes = 0;
    for (int i = 0; i < c->hist_n; i++) votes += c->hist[i];

    uint32_t dt_state = (s->t_sec >= c->last_t) ? (s->t_sec - c->last_t) : 1;
    (void)dt_state;

    switch (c->state) {
    case AG_ST_NORMAL:
        if (anomalous && (c->cusum_up > cfg->cusum_h || c->cusum_down > cfg->cusum_h ||
                          fabsf(z) > cfg->suspect_z)) {
            c->state = AG_ST_SUSPECT;
            c->suspect_since = s->t_sec;
            r.event = AG_EV_NONE; r.severity = AG_SEV_LOW;
        }
        break;
    case AG_ST_SUSPECT: {
        uint32_t aged = s->t_sec - c->suspect_since;
        c->state_age_s = aged;
        if (c->hist_n >= AG_PERSIST_M && votes >= AG_PERSIST_N && aged >= AG_PERSIST_MIN_S) {
            c->state = AG_ST_ALARM;
            /* classify the sustained shift */
            if (c->cusum_down > cfg->cusum_h && r.deficit > 2.0f) {
                r.event = AG_EV_WATER_STRESS; r.severity = AG_SEV_HIGH;
            } else if (c->cusum_up > cfg->cusum_h) {
                r.event = AG_EV_OVER_IRRIGATION; r.severity = AG_SEV_MED;
            } else {
                r.event = AG_EV_WATER_STRESS; r.severity = AG_SEV_MED;
            }
            if (fabsf(z) > cfg->alarm_z &&
                (c->cusum_up > cfg->cusum_h || c->cusum_down > cfg->cusum_h))
                r.severity = AG_SEV_HIGH;
        } else if (c->clean_run >= AG_RECOVERY_CLEAN && votes == 0) {
            c->state = AG_ST_NORMAL;
            c->suspect_since = 0;
            c->cusum_up = 0.f; c->cusum_down = 0.f;
        } else {
            r.event = AG_EV_NONE; r.severity = AG_SEV_LOW; /* still suspect */
        }
        break;
    }
    case AG_ST_ALARM:
        c->state_age_s = s->t_sec - c->suspect_since;
        if (c->clean_run >= AG_RECOVERY_CLEAN) {
            c->state = AG_ST_NORMAL;
            c->suspect_since = 0;
            c->cusum_up = 0.f; c->cusum_down = 0.f;
            r.event = AG_EV_NONE; r.severity = AG_SEV_NONE;
        } else {
            /* re-emit while the condition persists (caller rate-limits TX) */
            if (c->cusum_down > cfg->cusum_h) {
                r.event = AG_EV_WATER_STRESS; r.severity = AG_SEV_HIGH;
            } else if (c->cusum_up > cfg->cusum_h) {
                r.event = AG_EV_OVER_IRRIGATION; r.severity = AG_SEV_MED;
            }
        }
        break;
    }

    r.state = c->state;
    r.cusum_up = c->cusum_up; r.cusum_down = c->cusum_down;
    c->last_t = s->t_sec;
    return r;
}

/* ------------------------------------------------------------------ */
/* Adaptive cadence (§5.9): seconds until next evaluation               */
/* ------------------------------------------------------------------ */
static inline uint32_t ag_cadence_s(ag_state_t st, uint8_t probe_active) {
    if (probe_active || st == AG_ST_ALARM) return 5;
    if (st == AG_ST_SUSPECT) return 10;
    return 60;
}

/* Human-readable names (host + dashboard logs; zero cost on device
   unless actually called). */
static inline const char *ag_event_name(ag_event_t e) {
    switch (e) {
    case AG_EV_NONE: return "NONE";
    case AG_EV_IRRIGATION_OK: return "IRRIGATION_OK";
    case AG_EV_CONTINUOUS_LEAK: return "CONTINUOUS_LEAK";
    case AG_EV_LEAK_SEVERE: return "LEAK_SEVERE";
    case AG_EV_LEAK_OR_BYPASS: return "LEAK_OR_BYPASS";
    case AG_EV_OVER_IRRIGATION: return "OVER_IRRIGATION";
    case AG_EV_BLOCKAGE_NO_FLOW: return "BLOCKAGE_NO_FLOW";
    case AG_EV_BLOCKAGE_PARTIAL: return "BLOCKAGE_PARTIAL";
    case AG_EV_WATER_STRESS: return "WATER_STRESS";
    case AG_EV_SENSOR_STUCK: return "SENSOR_STUCK";
    case AG_EV_SENSOR_OUT_OF_RANGE: return "SENSOR_OUT_OF_RANGE";
    case AG_EV_SENSOR_DISCONNECTED: return "SENSOR_DISCONNECTED";
    case AG_EV_SENSOR_DRIFT: return "SENSOR_DRIFT";
    case AG_EV_SENSOR_SPIKE_BURST: return "SENSOR_SPIKE_BURST";
    case AG_EV_SENSOR_TIMEOUT: return "SENSOR_TIMEOUT";
    case AG_EV_FLOW_SENSOR_FAULT: return "FLOW_SENSOR_FAULT";
    case AG_EV_FLOW_NO_PULSE: return "FLOW_NO_PULSE";
    case AG_EV_FLOW_IMPLAUSIBLE: return "FLOW_IMPLAUSIBLE";
    case AG_EV_FLOW_NOISY: return "FLOW_NOISY";
    case AG_EV_EXTERNAL_EVENT: return "EXTERNAL_EVENT";
    case AG_EV_POWER_LOW: return "POWER_LOW";
    default: return "UNKNOWN";
    }
}

static inline const char *ag_sev_name(ag_sev_t s) {
    switch (s) {
    case AG_SEV_INFO: return "INFO";
    case AG_SEV_LOW: return "LOW";
    case AG_SEV_MED: return "MED";
    case AG_SEV_HIGH: return "HIGH";
    default: return "NONE";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* AGRIGUARD_ANOMALY_CORE_H */
