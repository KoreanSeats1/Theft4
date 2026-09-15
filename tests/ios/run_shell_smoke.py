#!/usr/bin/env python3
"""Exercise real UIKit foreground/background transitions on a booted simulator."""
import argparse
import json
import pathlib
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", required=True, help="Explicit booted simulator UUID")
    parser.add_argument("--app", required=True, type=pathlib.Path)
    parser.add_argument("--bundle-id", default="com.theft4.bringup")
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--screenshot", type=pathlib.Path)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error("cycles must be in 1..100")

    def simctl(*command):
        return subprocess.check_output(["xcrun", "simctl", *command], text=True, timeout=30).strip()

    simctl("install", args.simulator, str(args.app.resolve()))
    container = pathlib.Path(simctl("get_app_container", args.simulator, args.bundle_id, "data"))
    log = container / "Library/Application Support/Theft4/lifecycle.jsonl"
    launch = simctl("launch", "--terminate-running-process", args.simulator,
                    args.bundle_id, "--theft4-self-test")
    pid = int(launch.rsplit(":", 1)[1])

    def events():
        if not log.exists():
            return []
        # A running process may be appending the final line. Parse completed
        # records only; malformed completed records must still fail the test.
        lines = log.read_text().splitlines(keepends=True)
        return [entry for line in lines if line.endswith("\n")
                if (entry := json.loads(line))["pid"] == pid]

    def wait_for(event, count):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            entries = events()
            if any(e["event"].startswith("error.") or e["event"] == "selftest.failed" for e in entries):
                raise RuntimeError(f"App reported failure: {entries}")
            if sum(e["event"] == event for e in entries) >= count:
                return entries
            time.sleep(0.1)
        raise RuntimeError(f"Timed out waiting for {count} {event}: {events()}")

    initial = wait_for("selftest.completed", 1)
    if sum(e["event"] == "core.destroyed" for e in initial) != 3:
        raise RuntimeError("Startup test did not complete three teardown cycles")
    active = sum(e["event"] == "scene.active" for e in initial)
    background = sum(e["event"] == "scene.background" for e in initial)
    for cycle in range(args.cycles):
        simctl("launch", args.simulator, "com.apple.Preferences")
        wait_for("scene.background", background + cycle + 1)
        simctl("launch", args.simulator, args.bundle_id)
        wait_for("scene.active", active + cycle + 1)
        print(f"PASS UIKit cycle {cycle + 1}/{args.cycles}", flush=True)
    entries = events()
    states = [e["state"] for e in entries if e["event"] in ("scene.background", "scene.active")]
    if any(e["state"] != 2 for e in entries if e["event"] == "scene.background"):
        raise RuntimeError("Core was not paused before background entry")
    if any(e["state"] != 1 for e in entries if e["event"] == "scene.active"):
        raise RuntimeError("Core was not active on foreground activation")
    if any(e.get("guest_runtime_initialized", 0) for e in entries):
        raise RuntimeError("Probe unexpectedly initialized the guest runtime")
    if args.screenshot:
        simctl("io", args.simulator, "screenshot", str(args.screenshot.resolve()))
    print(json.dumps({"result": "PASS", "pid": pid, "uikit_cycles": args.cycles,
                      "teardown_cycles": 3, "state_samples": len(states), "log": str(log)}, indent=2))


if __name__ == "__main__":
    main()
