#!/usr/bin/env python3
"""Extract the observed BC-texture list from a Theft4 ASTC diagnostic export."""

import argparse
import csv
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="Diagnostic text export or observed-textures.jsonl")
    parser.add_argument("--csv", type=Path, help="Write the unique texture list as CSV")
    args = parser.parse_args()

    textures = {}
    for line in args.capture.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{") or '"sourceFormat"' not in line:
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(entry, dict) and entry.get("schemaVersion") == 1 and entry.get("key"):
            textures[entry["key"]] = entry

    if not textures:
        print("No observed ASTC texture records found.", file=sys.stderr)
        return 1

    ordered = [textures[key] for key in sorted(textures)]
    print(f"Unique observed BC textures: {len(ordered)}")
    for outcome in sorted({entry.get("outcome", "unknown") for entry in ordered}):
        count = sum(entry.get("outcome") == outcome for entry in ordered)
        print(f"  {outcome}: {count}")
    print(f"Input bytes: {sum(entry.get('sourceBytes', 0) for entry in ordered):,}")
    print(f"Output bytes: {sum(entry.get('resultBytes', 0) for entry in ordered):,}")

    if args.csv:
        columns = ["key", "sourceFormat", "width", "height", "mips", "sourceBytes",
                   "resultBytes", "cacheHit", "elapsedMs", "outcome"]
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(ordered)
        print(f"CSV: {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
