#!/usr/bin/env python3
"""Reject unoptimized iOS Release compiler commands before IPA packaging."""

import argparse
from pathlib import Path
import shlex
import sys


TARGETS = ("theft4_game_code", "theft4_core_bridge", "theft4_gta4_native_compile")


def check_arguments(arguments):
    tokens = shlex.split(arguments)
    optimization = None
    ndebug = False
    for token in tokens:
        if token in ("-O0", "-O1", "-O2", "-O3", "-Os", "-Oz", "-Og", "-Ofast"):
            optimization = token
        if token == "-DNDEBUG" or token.startswith("-DNDEBUG="):
            ndebug = True
        elif token == "-UNDEBUG":
            ndebug = False
    if optimization != "-O3" or not ndebug:
        raise ValueError(
            f"expected -O3 and NDEBUG, got optimization={optimization}, NDEBUG={ndebug}")


def verify(build_directory):
    checked = 0
    for target in TARGETS:
        response_directory = (build_directory / "build" / f"{target}.build" /
                              "Release-iphoneos" / "Objects-normal" / "arm64")
        responses = sorted(response_directory.glob("*-common-args.resp"))
        if not responses:
            raise ValueError(f"missing Release compiler arguments for {target}")
        for response in responses:
            try:
                check_arguments(response.read_text())
            except ValueError as error:
                raise ValueError(f"{target}: {response.name}: {error}") from error
            checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    args = parser.parse_args()
    try:
        checked = verify(args.build_directory)
    except (OSError, ValueError) as error:
        print(f"Release verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Release compiler verification passed: {checked} response files; "
          "AOT game, bridge and native renderer use -O3 and NDEBUG.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
