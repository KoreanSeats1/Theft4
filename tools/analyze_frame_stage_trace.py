#!/usr/bin/env python3
"""Summarize round1 long captures without confusing CPU observations with GPU time."""
import argparse
import csv
import json
import statistics
from collections import defaultdict


def summary(values):
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(values), "mean_ms": statistics.mean(values),
            "p95_ms": ordered[int((len(ordered) - 1) * .95)],
            "p99_ms": ordered[int((len(ordered) - 1) * .99)],
            "max_ms": max(values), "over_40_ms": sum(x > 40 for x in values)}


def analyze(rows):
    publications, configs, status = [], [], []
    stages = defaultdict(lambda: defaultdict(list))
    fences = defaultdict(lambda: defaultdict(list))
    losses = {"publication": 0, "stage": 0}
    for row in rows:
        kind = row["kind"]
        if kind == "frame":
            publications.append((int(row["frame"]), int(row["monotonic_ns"])))
        elif kind == "guest_stage":
            fields = dict(word.split("=", 1) for word in row["note"].split())
            stage, a, b = (int(fields[k]) for k in ("stage", "a", "b"))
            tick, frame = int(row["monotonic_ns"]), int(row["frame"])
            if stage in (8, 9):
                fences[a][stage].append(tick)
            elif frame:
                stages[frame][stage].append((tick, a, b))
        elif kind in ("lost", "stage_lost"):
            losses["stage" if kind == "stage_lost" else "publication"] += int(row["lost_count"])
        elif kind == "config":
            configs.append(row["note"])
        elif kind == "status":
            status.append(row["note"])
    # Skip capture-stop/resume boundaries and detected publication gaps.
    intervals = [(t - publications[i - 1][1]) / 1e6
                 for i, (f, t) in enumerate(publications) if i and f == publications[i - 1][0] + 1]
    metrics = defaultdict(list)
    reuse = changed = fallback = 0
    ambiguous = 0
    for frame, events in stages.items():
        ambiguous += any(len(v) != 1 for v in events.values())
        for begin, end, name in [(1, 2, "guest_submit_call"),
                                 (1, 4, "guest_submit_begin_to_worker_begin"),
                                 (4, 6, "worker_begin_to_queue_submit"),
                                 (6, 7, "queue_submit_call"),
                                 (4, 5, "worker_publish_call"),
                                 (1, 10, "guest_submit_begin_to_present_request")]:
            if len(events[begin]) == 1 and len(events[end]) == 1:
                duration = (events[end][0][0] - events[begin][0][0]) / 1e6
                if duration >= 0:
                    metrics[name].append(duration)
        if len(events[7]) == 1:
            tick, submission, slot = events[7][0]
            observed = fences[submission][9]
            if len(observed) == 1 and observed[0] >= tick:
                metrics["queue_submit_end_to_cpu_fence_observation_NOT_GPU_DURATION"].append((observed[0] - tick) / 1e6)
        if len(events[11]) == 1:
            reuse += events[11][0][1]
            changed += events[11][0][2]
        if len(events[12]) == 1:
            fallback += events[12][0][1]
    return {"configuration": configs, "last_status": status[-1] if status else "unknown",
            "lost_events": losses, "publication_intervals": summary(intervals),
            "cpu_stages": {name: summary(v) for name, v in metrics.items()},
            "constant_projection": {"reuse_bindings": reuse, "changed_version_reuses": changed,
                                    "fallback_bindings": fallback},
            "ambiguous_guest_frames": ambiguous,
            "limitations": ["Host-side observations only; no GPU duration or actual displayed timing.",
                            "Incomplete/duplicate phase pairs are omitted, not synthesized.",
                            "Reuses are binding counts, not measured time or bytes saved."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    args = parser.parse_args()
    with open(args.capture, newline="") as source:
        result = analyze(csv.DictReader(line for line in source if not line.startswith("#")))
    print(json.dumps(result, indent=2))
