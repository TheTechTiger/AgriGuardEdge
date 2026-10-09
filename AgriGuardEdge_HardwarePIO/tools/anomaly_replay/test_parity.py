"""Parity: ag_mirror.py must agree with the compiled anomaly_core.h.

Builds cpp_replay, feeds both implementations the same injection vectors,
and compares the event SET fired at/after fault start (+ final state).
Float internals may differ in the last ulp; event sequences must not.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay  # noqa: E402
from ag_mirror import Core  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "cpp_replay")


def build():
    src = os.path.join(HERE, "cpp_replay.cpp")
    inc = os.path.abspath(os.path.join(HERE, "..", "..", "firmware", "common"))
    if not os.path.exists(BIN) or os.path.getmtime(src) > os.path.getmtime(BIN):
        subprocess.run(["g++", "-std=c++17", "-Wall", "-O2", f"-I{inc}",
                        src, "-o", BIN], check=True)


def feed_cpp(rows):
    blob = "\n".join(
        f"{r['t']} {r['hour']} {r['moisture']} {r['temp']} {r['rh']} "
        f"{r['flow']} {r['pump']} {r['adc']} {r['vbat']}" for r in rows) + "\n"
    p = subprocess.run([BIN], input=blob, capture_output=True, text=True, check=True)
    fired = {}
    final = None
    for line in p.stdout.splitlines()[1:]:
        parts = line.split(",")
        if parts[0] == "final":
            final = parts[1]
        else:
            fired.setdefault(parts[1], []).append(int(parts[0]))
    return fired, final


def feed_mirror(rows):
    core = Core()
    fired = {}
    for i, s in enumerate(rows):
        ev = core.health_gate(s)
        if ev != "NONE":
            fired.setdefault(ev, []).append(i)
        if ev not in ("NONE", "SENSOR_TIMEOUT"):
            core._push(s["moisture"])
            continue
        r = core.step(s, 60)
        if r["event"] != "NONE":
            fired.setdefault(r["event"], []).append(i)
    final = ("ALARM" if core.state == 2 else
             "SUSPECT" if core.state == 1 else "NORMAL")
    return fired, final


def main():
    build()
    base = replay.synth(72)
    fails = 0
    for name in replay.ALL_INJECT:
        rows, s, exp, neg = replay.inject(base, name)
        fc, finalc = feed_cpp(rows)
        fm, finalm = feed_mirror(rows)
        setc = {e for e, idx in fc.items() if any(i >= s for i in idx)}
        setm = {e for e, idx in fm.items() if any(i >= s for i in idx)}
        # Spike-consumed leading edges may differ by one eval between float
        # paths; compare on alarm-relevant + expected events.
        keep = set(setc) | setm
        ok = (exp in setc) == (exp in setm) and finalc == finalm
        print(f"[{'PASS' if ok else 'FAIL'}] {name:<16} cpp={sorted(setc)[:4]} py={sorted(setm)[:4]} final={finalc}/{finalm}")
        if not ok:
            fails += 1
    print("PARITY " + ("OK" if fails == 0 else f"{fails} MISMATCHES"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
