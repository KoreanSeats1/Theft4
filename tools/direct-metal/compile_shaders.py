#!/usr/bin/env python3
"""Build offline iOS/macOS Metal libraries; retain each compiler diagnostic."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("sources", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--platform", choices=("ios", "macos"), default="ios")
parser.add_argument("--compiler-directory", type=Path)
parser.add_argument("--jobs", type=int, default=6)
args = parser.parse_args()
if args.compiler_directory is None:
    component = json.loads(subprocess.check_output([
        "xcodebuild", "-showComponent", "MetalToolchain", "-json"], text=True))
    root = Path(component["toolchainSearchPath"]) / "Metal.xctoolchain/usr/metal"
    args.compiler_directory = root / "current/bin"
    if not (args.compiler_directory / "metal").is_file():
        candidates = sorted(root.glob("*/bin/metal"))
        if not candidates:
            parser.error("Xcode's installed Metal compiler is unavailable")
        args.compiler_directory = candidates[-1].parent
args.output.mkdir(parents=True, exist_ok=True)
cache = (args.output / "module-cache").resolve()
target = "air64-apple-ios16.0" if args.platform == "ios" else "air64-apple-macos13.0"
standard = "ios-metal2.4" if args.platform == "ios" else "macos-metal2.4"

def compile_one(source):
    air = args.output / (source.stem + ".air")
    library = air.with_suffix(".metallib")
    log = air.with_suffix(".log")
    with log.open("w") as diagnostics:
        result = subprocess.run([
            str(args.compiler_directory / "metal"), "-target", target,
            "-std=" + standard, "-fmodules-cache-path=" + str(cache),
            "-fmetal-math-mode=safe", "-c", str(source), "-o", str(air)
        ], stdout=diagnostics, stderr=diagnostics)
        if result.returncode == 0:
            result = subprocess.run([
                str(args.compiler_directory / "metallib"), str(air), "-o", str(library)
            ], stdout=diagnostics, stderr=diagnostics)
    return {"shader": source.stem, "compiled": result.returncode == 0,
            "diagnostic": str(log)}

sources = sorted(args.sources.glob("*.metal"))
manifest = args.sources / "manifest.tsv"
if not manifest.is_file():
    parser.error("The exported manifest must accompany the Metal shader sources")
with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
    results = list(pool.map(compile_one, sources))
report = {"platform": args.platform, "target": target, "standard": standard,
          "sources": len(results), "compiled": sum(r["compiled"] for r in results),
          "rejected": [r for r in results if not r["compiled"]]}
(args.output / "COMPILATION.json").write_text(json.dumps(report, indent=2) + "\n")
if sources and not report["rejected"] and manifest.resolve() != (args.output / "manifest.tsv").resolve():
    shutil.copy2(manifest, args.output / "manifest.tsv")
print(json.dumps(report))
raise SystemExit(0 if sources and not report["rejected"] else 1)
