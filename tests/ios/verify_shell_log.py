#!/usr/bin/env python3
"""Verify M2 evidence copied from Theft4's own sandbox (simulator or device)."""
import argparse
import collections
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--platform", choices=("ios-arm64", "ios-simulator-arm64"), required=True)
    parser.add_argument("--pid", type=int, help="Defaults to the last completed self-test process")
    parser.add_argument("--cycles", type=int, default=20)
    args = parser.parse_args()
    rows = [json.loads(line) for line in args.log.read_text().splitlines()]
    completed = [r for r in rows if r["event"] == "selftest.completed"]
    if not completed:
        raise SystemExit("FAIL: no completed startup self-test")
    pid = args.pid if args.pid is not None else completed[-1]["pid"]
    rows = [r for r in rows if r["pid"] == pid]
    counts = collections.Counter(r["event"] for r in rows)
    if args.cycles < 0:
        parser.error("cycles cannot be negative")
    if (counts["selftest.completed"] != 1 or counts["core.destroyed"] != 3 or
            counts["scene.background"] < args.cycles or counts["scene.active"] < args.cycles + 1):
        raise SystemExit(f"FAIL: incomplete lifecycle evidence: {counts}")
    snapshots = [r for r in rows if "platform" in r]
    if not snapshots:
        raise SystemExit("FAIL: no native core snapshots")
    for row in rows:
        event = row["event"]
        if event.startswith("error.") or event == "selftest.failed":
            raise SystemExit(f"FAIL: {row}")
        if event == "scene.background" and row.get("state") != 2:
            raise SystemExit(f"FAIL: core not paused on background: {row}")
        if event == "scene.active" and row.get("state") != 1:
            raise SystemExit(f"FAIL: core not active on foreground: {row}")
    for row in snapshots:
        if (row["platform"] != args.platform or row["abi"] != 1 or row["page_bytes"] <= 0 or
                row["guest_runtime_initialized"] != 0 or not row["core_version"]):
            raise SystemExit(f"FAIL: unexpected core snapshot: {row}")
    print(json.dumps({"result": "PASS", "pid": pid, "platform": args.platform,
                      "page_bytes": snapshots[-1]["page_bytes"], "events": dict(counts)}, indent=2))


if __name__ == "__main__":
    main()
