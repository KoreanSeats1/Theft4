#!/usr/bin/env python3
"""Check the M1 probe or M2 UIKit Mach-O platform and dylib closure."""

import argparse
from pathlib import Path
import re
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--platform", choices=("device", "simulator"), required=True)
    parser.add_argument("--uikit", action="store_true", help="Check the M2 Theft4 app")
    args = parser.parse_args()
    binary = str(args.binary.resolve(strict=True))
    arch = output("/usr/bin/lipo", "-archs", binary).strip()
    if arch != "arm64":
        raise SystemExit(f"Expected only arm64, found {arch}")
    commands = output("/usr/bin/otool", "-l", binary)
    match = re.search(r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (\S+)", commands)
    expected = {"device": {"2", "IOS"}, "simulator": {"7", "IOSSIMULATOR"}}[args.platform]
    if not match or match.group(1) not in expected:
        raise SystemExit("Wrong or missing iOS Mach-O platform")
    allowed = {
        "/System/Library/Frameworks/Foundation.framework/Foundation",
        "/usr/lib/libSystem.B.dylib",
        "/usr/lib/libc++.1.dylib",
        "/usr/lib/libobjc.A.dylib",
    }
    if args.uikit:
        allowed.update({
            "/System/Library/Frameworks/UIKit.framework/UIKit",
            "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
        })
    dependencies = output("/usr/bin/otool", "-L", binary).splitlines()[1:]
    unexpected = [line.strip().split(" (", 1)[0] for line in dependencies
                  if line.strip().split(" (", 1)[0] not in allowed]
    if unexpected:
        raise SystemExit(f"Unexpected dynamic dependencies: {unexpected}")
    # The static core must not acquire native code through dynamic loading.
    undefined = output("/usr/bin/nm", "-u", binary)
    forbidden = {"_dlopen", "_dlsym", "_pthread_jit_write_protect_np"}
    imports = {line.split()[-1] for line in undefined.splitlines() if line.split()}
    if forbidden & imports:
        raise SystemExit(f"Unexpected native-code imports: {forbidden & imports}")
    if args.uikit:
        if "_UIApplicationMain" not in imports:
            raise SystemExit("Missing UIKit application entry point")
        symbols = output("/usr/bin/nm", "-gU", binary)
        for symbol in ("_theft4_core_create", "_theft4_core_get_snapshot", "_theft4_core_destroy"):
            if not re.search(rf"\bT {symbol}$", symbols, re.MULTILINE):
                raise SystemExit(f"Missing statically linked bridge function: {symbol}")
    print(f"PASS: {args.platform} arm64; correct Mach-O platform; system libraries only; static core")


if __name__ == "__main__":
    main()
