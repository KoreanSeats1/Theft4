#!/usr/bin/env python3
"""Check a signed macOS app's deployment target and runtime dependency closure."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess


def output(*command: str) -> str:
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def version(value: str) -> tuple[int, ...]:
    parts = tuple(int(part) for part in value.split('.'))
    return parts + (0,) * (3 - len(parts))


def load_commands(binary: Path) -> dict:
    commands = re.split(r'Load command \d+\n', output('otool', '-l', str(binary)))
    result = {'dependencies': [], 'rpaths': [], 'minimums': []}
    for command in commands:
        kind = re.search(r'^\s*cmd (LC_\w+)', command, re.M)
        if not kind:
            continue
        kind = kind[1]
        if kind in ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB', 'LC_LOAD_UPWARD_DYLIB'):
            result['dependencies'].append(re.search(r'\n\s*name (.*?) \(offset', command)[1])
        elif kind == 'LC_RPATH':
            result['rpaths'].append(re.search(r'\n\s*path (.*?) \(offset', command)[1])
        elif kind in ('LC_BUILD_VERSION', 'LC_VERSION_MIN_MACOSX'):
            result['minimums'].append(re.search(r'\n\s*(?:minos|version) ([\d.]+)', command)[1])
    return result


def verify(bundle: Path, architecture: str | None = None) -> dict:
    bundle = bundle.resolve(strict=True)
    info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    minimum = info['LSMinimumSystemVersion']
    executable = bundle / 'Contents/MacOS' / info['CFBundleExecutable']
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError(f'Missing or non-executable app binary: {executable}')
    binaries = sorted({executable, *(p.resolve() for p in (bundle / 'Contents').rglob('*.dylib'))})
    commands = {path: load_commands(path) for path in binaries}

    def expand(path: str, loader: Path) -> Path:
        return Path(path.replace('@executable_path', str(executable.parent))
                    .replace('@loader_path', str(loader.parent))).resolve()

    for binary, data in commands.items():
        if not binary.is_relative_to(bundle):
            raise RuntimeError(f'Library symlink escapes the app: {binary}')
        if architecture and architecture not in output('lipo', '-archs', str(binary)).split():
            raise RuntimeError(f'{binary.name} lacks architecture {architecture}')
        if not data['minimums'] or any(version(v) > version(minimum) for v in data['minimums']):
            raise RuntimeError(f'{binary.name}: Mach-O minimum {data["minimums"]} exceeds app minimum {minimum}')
        if binary == executable and any(version(v) != version(minimum) for v in data['minimums']):
            raise RuntimeError(f'Executable minimum {data["minimums"]} disagrees with Info.plist {minimum}')
        rpaths = [expand(p, binary) for p in data['rpaths']]
        rpaths += [expand(p, executable) for p in commands[executable]['rpaths']]
        for dependency in data['dependencies']:
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue  # Apple libraries may exist only in dyld's shared cache.
            if dependency.startswith('@rpath/'):
                candidates = [(p / dependency.removeprefix('@rpath/')).resolve() for p in rpaths]
            elif dependency.startswith(('@loader_path/', '@executable_path/')):
                candidates = [expand(dependency, binary)]
            else:
                raise RuntimeError(f'{binary.name} depends on an external path: {dependency}')
            if not any(p.is_relative_to(bundle) and p.is_file() for p in candidates):
                raise RuntimeError(f'{binary.name}: dependency is missing from app: {dependency}')
    output('codesign', '--verify', '--deep', '--strict', str(bundle))
    return {'passed': True, 'minimum_macos': minimum, 'architecture': architecture,
            'binaries_checked': len(binaries), 'signature_valid': True,
            'external_non_system_dependencies': []}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--arch', choices=('arm64', 'x86_64'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = verify(args.bundle, args.arch)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
