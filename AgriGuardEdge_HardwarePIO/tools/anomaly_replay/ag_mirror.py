"""Python behavioral mirror of AgriGuardEdge_HardwarePIO/firmware/common/anomaly_core.h.

Used by replay.py for fast metrics/sweeps. The C++ header remains the
normative implementation (AgriGuardEdge_HardwarePIO/firmware + tools/anomaly_replay/cpp_replay.cpp);
test_parity.py checks this mirror agrees with the compiled header on the
injection suite. Keep constants in sync with the header by hand — they are
listed in one block below so a diff is trivial.
"""
import math

# --- sync with anomaly_core.h ---
PULSES_PER_LITRE_NOMINAL = 450.0  # ZJ-S201: F = 7.5 x Q
FLOW_ALPHA = 0.25
FLOW_RATE_MAX_LPM = 12.0  # rig max observed 8 L/min + 50 % margin
LEAK_EPS_LPM = 0.30
BASELINE_ALPHA = 0.05
SIGMA_FLOOR = 0.40
SOFT_BAND_SIGMA = 2.0
CUSUM_K = 1.0  # fit via replay sweep (was 0.5)
CUSUM_H = 10.0  # fit via replay sweep (was 5)
SUSPECT_Z = 2.0
ALARM_Z = 3.0
PERSIST_M = 5
PERSIST_N = 3
PERSIST_MIN_S = 90
RECOVERY_CLEAN = 3
STUCK_K = 8
STUCK_MAD_PCT = 0.15
SPIKE_MAD_MULT = 6.0
V_TARGET_ML = 250.0
EXT_BLANK = 48
BATT_LOW_V = 3.40
BASELINE_WIN = 16
R_OK_LO, R_OK_HI, R_PART_LO, R_LEAK_LO = 0.80, 1.15, 0.45, 0.05
V_WETTED_ML = 40.0

NORMAL, SUSPECT, ALARM = 0, 1, 2


def median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return 0.0
    if n & 1:
        return s[n // 2]
    return 0.5 * (s[n // 2 - 1] + s[n // 2])


def mad_sigma(xs):
    if not xs:
        return SIGMA_FLOOR
    med = median(xs)
    mad = median([abs(x - med) for x in xs])
    return max(1.4826 * mad, SIGMA_FLOOR)


def conservation_verdict(retention, v_del_ml):
    if v_del_ml <= 1.0:
        return "NONE"
    if retention > R_OK_HI:
        return "OVER_IRRIGATION"
    if retention >= R_OK_LO:
        return "IRRIGATION_OK"
    if retention >= R_PART_LO:
        return "BLOCKAGE_PARTIAL"
    if retention >= R_LEAK_LO:
        return "LEAK_OR_BYPASS"
    return "LEAK_SEVERE"


class Core:
    def __init__(self, cusum_k=CUSUM_K, cusum_h=CUSUM_H,
                 suspect_z=SUSPECT_Z, alarm_z=ALARM_Z, batt_low=3.4):
        self.moist_base = [0.0] * 24
        self.sigma_base = [1.0] * 24
        self.et_rate = [0.15] * 24
        self.base_init = [False] * 24
        self.win, self.win_d = [], []
        self.prev, self.prev_valid = 0.0, False
        self.cusum_up = self.cusum_down = self.ewma_z = 0.0
        self.state = NORMAL
        self.hist = []
        self.suspect_since = 0
        self.clean_run = 0
        self.r_ref = 0.0
        self.flow_stale_s = 0
        self.leak_windows = 0
        self.last_t = 0
        self.evals = 0
        self.ext_blank = 0
        self.ext_calm = 0
        self.prev_exc = False
        self.prev_big = False
        self.k, self.h = cusum_k, cusum_h
        self.sz, self.az = suspect_z, alarm_z
        self.batt_low = batt_low

    # ① health gate
    def health_gate(self, s):
        # Rail first: a disconnected probe floats to a rail; its calibrated %
        # would also read out-of-range, so the raw rail is the stronger signal.
        if s.get("adc", -1) >= 0 and (s["adc"] <= 2 or s["adc"] >= 4093):
            return "SENSOR_DISCONNECTED"
        if not (2.0 <= s["moisture"] <= 98.0):
            return "SENSOR_OUT_OF_RANGE"
        if self.prev_valid and len(self.win_d) >= STUCK_K:
            med = median(self.win_d)
            mad = median([abs(x - med) for x in self.win_d])
            if mad * 1.4826 < STUCK_MAD_PCT and med < STUCK_MAD_PCT:
                return "SENSOR_STUCK"
            spread = max(1.4826 * mad, SIGMA_FLOOR)
            dx = abs(s["moisture"] - self.prev)
            if dx > SPIKE_MAD_MULT * spread and dx > 3.0:
                return "SENSOR_SPIKE_BURST"
        if math.isnan(s.get("temp", 26.0)) or math.isnan(s.get("rh", 60.0)):
            if self.evals > 0:
                return "SENSOR_TIMEOUT"
        if s["flow"] < -0.05 or s["flow"] > FLOW_RATE_MAX_LPM:
            return "FLOW_IMPLAUSIBLE"
        if 0.1 < s.get("vbat", 3.9) < self.batt_low:
            return "POWER_LOW"
        return "NONE"

    def _push(self, m):
        if self.prev_valid:
            dx = abs(m - self.prev)
            self.win.append(m)
            self.win_d.append(dx)
            if len(self.win) > BASELINE_WIN:
                self.win.pop(0)
                self.win_d.pop(0)
        else:
            self.win.append(m)
            if len(self.win) > BASELINE_WIN:
                self.win.pop(0)
        self.prev, self.prev_valid = m, True

    # ②③④⑥
    def step(self, s, dt):
        r = {"event": "NONE", "severity": "NONE", "state": self.state,
             "z": 0.0, "cusum_up": self.cusum_up, "cusum_down": self.cusum_down,
             "baseline": 0.0, "sigma": 1.0, "deficit": 0.0, "flow": s["flow"]}
        self.evals += 1
        h = s["hour"] % 24

        if not s["pump"] and s["flow"] > LEAK_EPS_LPM:  # ⑥ leak watch ★
            self.leak_windows += 1
            if self.leak_windows >= 2:
                self.state = ALARM
                r.update(event="CONTINUOUS_LEAK", severity="HIGH", state=ALARM)
                return r
        elif not s["pump"]:
            self.leak_windows = 0
        if s["pump"] and s["flow"] < 0.2:  # ⑥ blockage
            self.flow_stale_s += dt
            if self.flow_stale_s >= 15:
                self.state = ALARM
                r.update(event="BLOCKAGE_NO_FLOW", severity="HIGH", state=ALARM)
                return r
        else:
            self.flow_stale_s = 0

        if not self.base_init[h]:  # ② baseline
            self.moist_base[h] = s["moisture"]
            self.sigma_base[h] = 1.0
            self.base_init[h] = True
        base = self.moist_base[h]
        sigma = max(self.sigma_base[h], SIGMA_FLOOR)
        z = (s["moisture"] - base) / sigma
        r.update(z=z, baseline=base, sigma=sigma, deficit=base - s["moisture"])

        # Rain-tail ride-out (mirrors anomaly_core.h): frozen baseline and
        # CUSUM/persistence, STICKY exit (3x |z|<1). Flow watches above live.
        if self.ext_blank > 0:
            if abs(z) < 1.0:
                self.ext_calm += 1
            else:
                self.ext_calm = 0
            if self.ext_calm >= 3:
                self.ext_blank = 0
                self.ext_calm = 0
        if self.ext_blank > 0:
            self.ext_blank -= 1
            self.cusum_up = self.cusum_down = 0.0
            self._push(s["moisture"])
            self.last_t = s["t"]
            r.update(event="NONE", severity="NONE", cusum_up=0.0,
                     cusum_down=0.0, state=self.state)
            return r

        self.ewma_z += 0.2 * (z - self.ewma_z)  # ③ CUSUM
        self.cusum_up = max(0.0, self.cusum_up + (z - self.k))
        self.cusum_down = max(0.0, self.cusum_down + (-z - self.k))
        r.update(cusum_up=self.cusum_up, cusum_down=self.cusum_down)

        anomalous = (abs(z) > self.sz or self.cusum_up > self.h
                     or self.cusum_down > self.h)
        exc = abs(z) > self.sz
        big = abs(z) > self.az
        cusum_trip = self.cusum_up > self.h or self.cusum_down > self.h

        if z > 4.0:  # rain negative control: INFO, never alerts
            r.update(event="EXTERNAL_EVENT", severity="INFO")
            self.moist_base[h] += 0.5 * BASELINE_ALPHA * (s["moisture"] - base)
            self.cusum_up = self.cusum_down = 0.0
            self.ext_blank = EXT_BLANK
            self._push(s["moisture"])
            self.last_t = s["t"]
            r["state"] = self.state
            return r

        if not anomalous and abs(z) < SOFT_BAND_SIGMA:  # guarded adaptation
            self.moist_base[h] += BASELINE_ALPHA * (s["moisture"] - base)
            if len(self.win) >= 4:
                ws = mad_sigma(self.win)
                self.sigma_base[h] = max(
                    self.sigma_base[h] + BASELINE_ALPHA * (ws - self.sigma_base[h]),
                    SIGMA_FLOOR)
            if 0 < dt < 900 and self.prev_valid:
                drift = (s["moisture"] - self.prev) * 3600.0 / dt
                if -5.0 < drift < 0.0:
                    self.et_rate[h] += BASELINE_ALPHA * ((-drift) - self.et_rate[h])
            self.clean_run += 1
        else:
            self.clean_run = 0
        self._push(s["moisture"])

        self.hist.append(1 if anomalous else 0)  # ④ persistence
        if len(self.hist) > PERSIST_M:
            self.hist.pop(0)
        votes = sum(self.hist)

        if self.state == NORMAL:
            # Stricter entry than one bad sample (mirrors header): CUSUM
            # confirmation or two consecutive excursions; votes counted
            # fresh from entry so pre-history cannot promote.
            if cusum_trip or (exc and self.prev_exc):
                self.state = SUSPECT
                self.suspect_since = s["t"]
                self.hist = []
                self.clean_run = 0
                r.update(severity="LOW")
            self.prev_exc = exc
            self.prev_big = big
        elif self.state == SUSPECT:
            aged = s["t"] - self.suspect_since
            fast = big and self.prev_big  # >=4σ twice: promote at once (≤120 s)
            if (len(self.hist) >= PERSIST_M and votes >= PERSIST_N and aged >= PERSIST_MIN_S) or fast:
                self.state = ALARM
                if self.cusum_down > self.h and r["deficit"] > 2.0:
                    r.update(event="WATER_STRESS", severity="HIGH")
                elif self.cusum_up > self.h:
                    r.update(event="OVER_IRRIGATION", severity="MED")
                else:
                    r.update(event="WATER_STRESS", severity="MED")
                if abs(z) > self.az and (self.cusum_up > self.h or self.cusum_down > self.h):
                    r["severity"] = "HIGH"
            elif self.clean_run >= RECOVERY_CLEAN and votes == 0:
                self.state = NORMAL
                self.suspect_since = 0
                self.cusum_up = self.cusum_down = 0.0
            else:
                r.update(severity="LOW")
            self.prev_exc = exc
            self.prev_big = big
        else:  # ALARM
            if self.clean_run >= RECOVERY_CLEAN:
                self.state = NORMAL
                self.suspect_since = 0
                self.cusum_up = self.cusum_down = 0.0
                r.update(event="NONE", severity="NONE")
            elif self.cusum_down > self.h:
                r.update(event="WATER_STRESS", severity="HIGH")
            elif self.cusum_up > self.h:
                r.update(event="OVER_IRRIGATION", severity="MED")
        r["state"] = self.state
        r.update(cusum_up=self.cusum_up, cusum_down=self.cusum_down)
        self.last_t = s["t"]
        return r

    # ⑤ conservation
    def probe_evaluate(self, v_del_ml, d_moist_pct, v_et_ml, is_reference=False):
        v_soil = max(0.0, d_moist_pct * V_WETTED_ML)
        residual = (v_del_ml - v_soil - v_et_ml) / v_del_ml if v_del_ml > 1 else 0.0
        retention = (v_soil + v_et_ml) / v_del_ml if v_del_ml > 1 else 0.0
        if is_reference and v_del_ml > 1:
            self.r_ref = retention
            return "IRRIGATION_OK", residual, 1.0
        vs_ref = retention / self.r_ref if self.r_ref > 0.001 and v_del_ml > 1 else 1.0
        v = conservation_verdict(retention, v_del_ml)
        if v == "OVER_IRRIGATION":
            v = "SENSOR_DRIFT"
        return v, residual, vs_ref


def cadence_s(state, probe_active):
    if probe_active or state == ALARM:
        return 5
    if state == SUSPECT:
        return 10
    return 60
