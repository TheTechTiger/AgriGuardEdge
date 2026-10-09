#!/usr/bin/env python3
"""Fresh-start wipe for the AgriGuard Edge dashboard.

Deletes every node-ingested row (messages, events, summaries, energy_log,
probe_results) so all panels render empty. Auth (userDetails) and the
notification bell are untouched. Safe to run while the server is up —
SQLite serialises the extra connection; the running app sees the wipe
on its next query (dashboard refreshes every 15 s).

Usage:  python3 reset_db.py [--yes]
"""
import os
import sqlite3
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(BASE_DIR, "AgriGuardEdgeDB.db")
TABLES = ("messages", "events", "summaries", "energy_log", "probe_results")


def main():
    if not os.path.exists(DB):
        print(f"no database yet at {DB} — nothing to clear.")
        return 0
    if "--yes" not in sys.argv:
        ans = input("Delete ALL node data (ingest tables only)? [y/N] ").strip()
        if ans.lower() != "y":
            print("aborted.")
            return 1
    db = sqlite3.connect(DB)
    before = {}
    for t in TABLES:
        try:
            before[t] = db.execute(f"SELECT COUNT(*) FROM {t}").fetchone()[0]
        except sqlite3.OperationalError:
            before[t] = 0
    for t in TABLES:
        try:
            db.execute(f"DELETE FROM {t}")
        except sqlite3.OperationalError:
            pass
    db.commit()
    db.execute("VACUUM")
    db.close()
    total = sum(before.values())
    print(f"cleared {total} rows: " +
          ", ".join(f"{t}={before[t]}" for t in TABLES))
    users = sqlite3.connect(DB).execute(
        "SELECT COUNT(*) FROM userDetails").fetchone()[0]
    print(f"kept: userDetails={users} (+ notifications untouched)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
