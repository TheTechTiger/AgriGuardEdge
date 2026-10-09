"""Host replay harness — arch §10.2/§10.5.

  python3 replay.py --csv data/mendeley.csv --inject all --report out/
  python3 replay.py --synthetic-hours 48 --inject stress,external_event --report out/
  python3 replay.py --sweep --report out/

Stdlib only. Behavioral mirror (ag_mirror.py) of AgriGuardEdge_HardwarePIO/firmware/common/
anomaly_core.h; test_parity.py checks the mirror against the compiled
header via cpp_replay. If --csv is omitted or headers are unrecognised,
falls back to a synthetic diurnal signal (documented fallback, §12.3).
"""
import argparse
import csv
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ag_mirror import Core, conservation_verdict  # noqa: E402

MOIST_KEYS = ("soil_moisture", "moisture", "soil_humidity")
TEMP_KEYS = ("env_temperature", "temperature", "soil_temperature", "temp")
RH_KEYS = ("env_humidity", "humidity", "rh")


def pick(row, keys, default):
    for k in keys:
        if k in row and row[k] not in ("", None):
            try:
                return float(row[k])
            except ValueError:
                pass
    return default


def load_csv(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError("empty csv")
    out = []
    for i, r in enumerate(rows):
        out.append({
            "t": i * 60, "hour": (i // 60) % 24,
            "moisture": pick(r, MOIST_KEYS, 42.0),
            "temp": pick(r, TEMP_KEYS, 26.0),
            "rh": pick(r, RH_KEYS, 60.0),
            "flow": 0.0, "pump": 0, "adc": 2000, "vbat": 3.9,
        })
    return out


def synth(hours=72, seed=7):
    rng = random.Random(seed)
    out = []
    n = hours * 60
    for i in range(n):
        h = (i // 60) % 24
        # σ=0.8: realistic capacitive-probe noise. (σ=0.3 bench-tests once
        # showed the STUCK gate firing on clean data — the 0.15 threshold
        # assumes real probe noise, so the synth must carry it.)
        m = 42.0 + 5.0 * math.sin(2 * math.pi * (h - 9) / 24.0) + rng.gauss(0, 0.8)
        out.append({
            "t": i * 60, "hour": h,
            "moisture": m,
            "temp": 26.0 + 6.0 * math.sin(2 * math.pi * (h - 14) / 24.0),
            "rh": 60.0 - 15.0 * math.sin(2 * math.pi * (h - 9) / 24.0),
            "flow": 0.0, "pump": 0, "adc": 2000, "vbat": 3.9,
        })
    return out


# --- injections: (name, expected_event, is_negative) ---
def inject(rows, name):
    rows = [dict(r) for r in rows]
    n = len(rows)
    # Fault starts mid-run so every hour bucket has a full day of training.
    # (A fault in a never-seen bucket is absorbed as that bucket's init —
    # cold-start limitation, documented in the report.)
    s = n // 2
    neg = False
    if name == "flatline":
        for r in rows[s:s + 60]:
            r["moisture"] = rows[s]["moisture"]
        exp = "SENSOR_STUCK"
    elif name == "rail":
        for r in rows[s:s + 10]:
            r["moisture"] = 99.5
            r["adc"] = 4095
        exp = "SENSOR_DISCONNECTED"
    elif name == "spike":
        for r in rows[s:s + 2]:
            r["moisture"] += 8.0
        exp = "SENSOR_SPIKE_BURST"
    elif name == "stress":
        for r in rows[s:s + 40]:
            r["moisture"] -= 4.5
        exp = "WATER_STRESS"
    elif name == "external_event":
        for j, r in enumerate(rows[s:s + 30]):
            r["moisture"] += 9.0 * math.exp(-j / 8.0)
        exp = "EXTERNAL_EVENT"
        neg = True  # must NOT raise ALARM
    elif name == "dht_dropout":
        for r in rows[s:s + 5]:
            r["temp"] = float("nan")
            r["rh"] = float("nan")
        exp = "SENSOR_TIMEOUT"
    elif name == "continuous_leak":
        for r in rows[s:s + 4]:
            r["flow"] = 2.5
        exp = "CONTINUOUS_LEAK"
    elif name == "blockage":
        for r in rows[s:s + 5]:
            r["pump"] = 1
            r["flow"] = 0.0
        exp = "BLOCKAGE_NO_FLOW"
    elif name == "flow_death":
        for r in rows[s:s + 5]:
            r["pump"] = 1
            r["flow"] = 0.0
        exp = "BLOCKAGE_NO_FLOW"
    else:
        raise ValueError(f"unknown injection {name}")
    return rows, s, exp, neg


ALL_INJECT = ["flatline", "rail", "spike", "stress", "external_event",
              "dht_dropout", "continuous_leak", "blockage", "flow_death"]
PROBE_INJECT = ["slow_leak", "severe_leak", "drift"]


def run_rows(rows, core, dt=60):
    """Returns (fired, alarms, us_per_eval).

    Firmware policy mirrored here: the health gate's verdict is ALWAYS
    recorded, and the moisture distribution analytics still run on that
    eval (a DHT dropout must not blind the soil channel). A hard fault
    (stuck/rail/spike/flow/power) additionally suppresses baseline
    learning for that eval — same as the firmware's exclude-from-baseline.
    """
    fired = {}
    alarms = 0
    t0 = time.perf_counter()
    for i, s in enumerate(rows):
        ev = core.health_gate(s)
        if ev != "NONE":
            fired.setdefault(ev, []).append(i)
        hard = ev not in ("NONE", "SENSOR_TIMEOUT")
        if not hard:
            r = core.step(s, dt)
            if r["event"] != "NONE":
                fired.setdefault(r["event"], []).append(i)
            if core.state == 2:
                alarms += 1
        else:
            core._push(s["moisture"])
    us = (time.perf_counter() - t0) / max(len(rows), 1) * 1e6
    return fired, alarms, us


def run_probe(core, retention_frac):
    """Reference cycle then a faulty cycle at retention_frac × reference."""
    core.probe_evaluate(250.0, 5.0, 3.0, is_reference=True)
    ref_ret = core.r_ref
    d_moist = ref_ret * retention_frac * 250.0 / 40.0
    t0 = time.perf_counter()
    v, residual, vs_ref = core.probe_evaluate(250.0, d_moist, 3.0)
    us = (time.perf_counter() - t0) * 1e6
    return v, residual, vs_ref, us


def evaluate(base, name, kh=None):
    core = Core(*(kh or ()))
    if name in PROBE_INJECT:
        frac = {"slow_leak": 0.2, "severe_leak": 0.01, "drift": 1.8}[name]
        exp = {"slow_leak": "LEAK_OR_BYPASS", "severe_leak": "LEAK_SEVERE",
               "drift": "SENSOR_DRIFT"}[name]
        v, residual, vs_ref, us = run_probe(core, frac)
        return {"injection": name, "expected": exp, "got": v,
                "detected": v == exp, "latency_evals": 0,
                "residual": round(residual, 3), "vs_ref": round(vs_ref, 3),
                "us_per_eval": round(us, 1), "negative": False}
    rows, s, exp, neg = inject(base, name)
    fired, alarms, us = run_rows(rows, core)
    # Methodology: detection counts only at/after fault start. A rare LOW
    # blip (e.g. noise spike) hours earlier must not satisfy — or break —
    # the latency of the injected fault. Clean-run behaviour is measured
    # separately as false_alarms_per_day.
    at = {e: min(i for i in idx if i >= s)
          for e, idx in fired.items() if any(i >= s for i in idx)}
    ALARM_CLASS = ("WATER_STRESS", "OVER_IRRIGATION", "CONTINUOUS_LEAK",
                   "BLOCKAGE_NO_FLOW", "LEAK_OR_BYPASS", "LEAK_SEVERE")
    if neg:
        ok = (exp in at) and not any(e in ALARM_CLASS for e in at)
        lat = at.get(exp, -1) - s if exp in at else -1
    else:
        ok = exp in at
        lat = at.get(exp, -1) - s if ok else -1
    return {"injection": name, "expected": exp,
            "got": sorted(at, key=lambda e: at[e])[:4],
            "detected": ok, "latency_evals": lat,
            "us_per_eval": round(us, 2), "negative": neg}


def false_alarms_per_day(base):
    core = Core()
    _, alarms, _ = run_rows(base, core)
    days = len(base) / 1440.0
    return round(alarms / max(days, 1e-6), 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default=None)
    ap.add_argument("--synthetic-hours", type=int, default=72)
    ap.add_argument("--inject", default="all",
                    help="comma list from: " + ",".join(ALL_INJECT + PROBE_INJECT) + " (or 'all')")
    ap.add_argument("--flow-profile", default="synthetic")
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--report", default="out")
    args = ap.parse_args()

    if args.csv:
        try:
            base = load_csv(args.csv)
            src = f"csv:{args.csv}"
        except Exception as e:
            print(f"CSV unusable ({e}); synthetic fallback (§12.3)")
            base = synth(args.synthetic_hours)
            src = "synthetic-fallback"
    else:
        base = synth(args.synthetic_hours)
        src = "synthetic"

    names = (ALL_INJECT + PROBE_INJECT) if args.inject == "all" \
        else [x.strip() for x in args.inject.split(",")]
    results = [evaluate(base, n) for n in names]
    fad = false_alarms_per_day(base)

    sweep = None
    if args.sweep:
        # K/H fit per §10.3: zero false alarms on the clean run AND both
        # distribution controls (stress detect, rain silent), then min latency.
        sweep = []
        for k in (0.5, 0.75, 1.0, 1.25):
            for h in (5.0, 7.0, 10.0):
                kh = (k, h, 2.0, 3.0)
                fa = false_alarms_per_day(base)
                # NOTE: Core() below uses defaults; recompute with kh:
                c0 = Core(*kh)
                _, alarms0, _ = run_rows(base, c0)
                fa = round(alarms0 / (len(base) / 1440.0), 2)
                st = evaluate(base, "stress", kh=kh)
                ex = evaluate(base, "external_event", kh=kh)
                sweep.append({"K": k, "H": h, "fa_per_day": fa,
                              "stress": bool(st["detected"]),
                              "stress_lat": st["latency_evals"],
                              "rain_silent": bool(ex["detected"])})
        print("K/H sweep (fa/day · stress detect/lat · rain silent):")
        for row in sweep:
            print(f"  K={row['K']:<4} H={row['H']:<4} fa={row['fa_per_day']:<7} "
                  f"stress={int(row['stress'])}/{row['stress_lat']:<4} rain={int(row['rain_silent'])}")

    det = sum(1 for r in results if r["detected"])
    os.makedirs(args.report, exist_ok=True)
    with open(os.path.join(args.report, "metrics.json"), "w") as f:
        json.dump({"source": src, "flow_profile": args.flow_profile,
                   "false_alarms_per_day": fad,
                   "detected": f"{det}/{len(results)}",
                   "results": results, "sweep": sweep}, f, indent=1)
    with open(os.path.join(args.report, "metrics.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["injection", "expected", "detected", "latency_evals", "us_per_eval"])
        for r in results:
            w.writerow([r["injection"], r["expected"], int(r["detected"]),
                        r["latency_evals"], r["us_per_eval"]])
    with open(os.path.join(args.report, "summary.md"), "w") as f:
        f.write(f"# Replay report ({src}, flow: {args.flow_profile})\n\n")
        f.write(f"Detected {det}/{len(results)} · false alarms/day on clean run: {fad}\n\n")
        f.write("Note: faults start mid-run so every hour bucket carries a full "
                "day of training. A fault landing in a never-seen bucket is "
                "absorbed as that bucket's init (cold-start limitation — the "
                "demo soaks a 10-min baseline first, §11 scenario 1).\n\n")
        for r in results:
            mark = "PASS" if r["detected"] else "FAIL"
            f.write(f"- [{mark}] {r['injection']}: expected {r['expected']}, "
                    f"latency {r['latency_evals']} evals, {r['us_per_eval']} µs/eval\n")
    for r in results:
        print(f"[{'PASS' if r['detected'] else 'FAIL'}] {r['injection']:<16} "
              f"expected={r['expected']:<20} latency={r['latency_evals']}")
    print(f"detected {det}/{len(results)} · false alarms/day: {fad}")
    return 0 if det == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
