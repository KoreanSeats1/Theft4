#!/usr/bin/env python3
"""Summarize exported xctrace time-profile XML; weights are sampled CPU time."""
import collections
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
ids = {e.get("id"): e for e in root.iter() if e.get("id")}


def resolve(element):
    if element is None:
        return None
    return ids.get(element.get("ref"), element)


if len(sys.argv) > 2:
    binary_path = sys.argv[2]
    binary = next(e for e in root.iter("binary") if e.get("name") == "Theft4")
    uuid = subprocess.check_output(["xcrun", "dwarfdump", "--uuid", binary_path], text=True)
    if binary.get("UUID", "missing") not in uuid:
        raise SystemExit("Refusing symbolication: executable UUID does not match recording")
    frames = [e for e in root.iter("frame")
              if e.get("addr") and resolve(e.find("binary")) is binary]
    addresses = list(dict.fromkeys(e.get("addr") for e in frames))
    symbols = {}
    for start in range(0, len(addresses), 512):
        batch = addresses[start:start + 512]
        result = subprocess.check_output(
            ["xcrun", "atos", "-arch", "arm64", "-o", binary_path,
             "-l", binary.get("load-addr"), *batch], text=True).splitlines()
        if len(result) != len(batch):
            raise SystemExit("Unexpected atos result count")
        for address, symbol in zip(batch, result):
            symbols[address] = re.sub(r" \(in Theft4\).*", "", symbol)
    for frame in frames:
        frame.set("name", symbols[frame.get("addr")])


threads = collections.Counter()
leaves = collections.defaultdict(collections.Counter)
inclusive = collections.defaultdict(collections.Counter)
for row in root.iter("row"):
    state = resolve(row.find("thread-state"))
    if state is None or state.text != "Running":
        continue
    weight = resolve(row.find("weight"))
    thread = resolve(row.find("thread"))
    stack = resolve(row.find("tagged-backtrace"))
    if weight is None or not weight.text or thread is None:
        continue
    seconds = int(weight.text) / 1e9
    name = thread.get("fmt", "unknown")
    if "Theft4" not in name:
        continue
    threads[name] += seconds
    if stack is None:
        continue
    frames = [resolve(f).get("name", resolve(f).get("addr", "?")) for f in stack.findall("frame")]
    if frames:
        leaves[name][frames[0]] += seconds
    for frame in set(frames):
        inclusive[name][frame] += seconds

for name, total in threads.most_common(12):
    print(f"\n{total:.3f} CPU-s {name}")
    print("  LEAF")
    for function, seconds in leaves[name].most_common(8):
        print(f"    {seconds:.3f} {function}")
    print("  INCLUSIVE")
    for function, seconds in inclusive[name].most_common(8):
        print(f"    {seconds:.3f} {function}")
