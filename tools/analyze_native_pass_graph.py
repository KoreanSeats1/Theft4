#!/usr/bin/env python3
"""Inspect attachment chronology in a Theft4 native detailed capture.

This is an *observer*, not a dependency proof: the existing CSV has RT0 and
depth identities, but not all color targets, sampled reads, barriers, Metal
encoder boundaries, or per-subresource write regions. Never use this report to
remove a load, store, resolve, draw, or synchronization operation by itself.
"""

import argparse
import collections
import csv
from pathlib import Path


def chosen_frame(capture: Path, requested: int | None) -> int:
    if requested is not None:
        return requested
    with (capture / "native-performance-latest.csv").open(newline="") as source:
        rows = csv.DictReader(source)
        return int(max(rows, key=lambda row: float(row["cpu_frame_interval_ms"]))["frame"])


def attachment(row: dict[str, str], stem: str) -> tuple[str, str, str, str] | None:
    handle = row[f"{stem}_handle"]
    if not handle or int(handle, 16) == 0:
        return None
    return (handle, row[f"{stem}_generation"], row[f"{stem}_format"],
            row[f"{stem}_declared_samples"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="folder containing native-performance-*.csv")
    parser.add_argument("--frame", type=int, help="native frame; default is worst publication interval")
    args = parser.parse_args()
    frame = chosen_frame(args.capture, args.frame)
    nodes = []
    with (args.capture / "native-performance-gpu-passes.csv").open(newline="") as source:
        for row in csv.DictReader(source):
            if int(row["frame"]) == frame:
                nodes.append(row)
    if not nodes:
        parser.error(f"frame {frame} not present")

    # Each edge means that two observed ranges share an RT0 or depth identity
    # in submission order. The CSV cannot establish whether either read or
    # wrote a particular subresource; the edge is deliberately conservative.
    last_use = {}
    edges = []
    attachment_uses = collections.Counter()
    reentries = collections.Counter()
    last_target = None
    for index, node in enumerate(nodes):
        target = (attachment(node, "rt0"), attachment(node, "depth"))
        if target != last_target and target in reentries:
            reentries[target] += 1
        else:
            reentries.setdefault(target, 0)
        last_target = target
        for kind, resource in (("RT0", target[0]), ("depth", target[1])):
            if resource is None:
                continue
            key = (kind, resource)
            attachment_uses[key] += 1
            if key in last_use:
                edges.append((last_use[key], index, kind))
            last_use[key] = index

    print(f"capture={args.capture} frame={frame} ranges={len(nodes)} "
          f"attachment-chronology-edges={len(edges)}")
    print("LIMITS: RT0/depth only; sampled reads, RT1+, regions, resolves, aliases, "
          "load/store actions and actual Metal scopes are UNKNOWN.")
    print("These are shared-attachment ordering edges, NOT removable-pass proofs.")
    print("\nLargest timed ranges (approximate GPU queue boundaries):")
    for index, node in sorted(enumerate(nodes), key=lambda item: float(item[1]["gpu_ms"]),
                              reverse=True)[:18]:
        print(f"  #{index:5} {float(node['gpu_ms']):6.2f}ms "
              f"draws={node['draws']:>5} {node['range'][:45]:45} "
              f"rt0={node['rt0_handle']} depth={node['depth_handle']}")
    print("\nMost reused observed attachments:")
    for (kind, resource), uses in attachment_uses.most_common(12):
        print(f"  {kind:5} handle={resource[0]} generation={resource[1]} "
              f"format={resource[2]} samples={resource[3]} ranges={uses}")
    print("\nAttachment-set reentries after another target (diagnostic only):")
    for target, count in reentries.most_common(10):
        if count:
            rt = target[0][0] if target[0] else "none"
            depth = target[1][0] if target[1] else "none"
            print(f"  RT0={rt} depth={depth} reentries={count}")


if __name__ == "__main__":
    main()
