#!/usr/bin/env python3
"""Read-only comparison of decoded packaged shader stages and observed log IDs.

This reports observed coverage only. A clean log is not complete gameplay
coverage, and byte-identical compiled modules do not prove guest semantic aliases.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
from validate_shader_preservation import NativeDecoders, cache_inventory

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--smolv-library', type=Path, required=True)
    p.add_argument('--zstd-library')
    p.add_argument('--log', type=Path, action='append', default=[])
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    inventory = cache_inventory(args.cache, NativeDecoders(args.smolv_library, args.zstd_library))
    entries = inventory['entries']
    observations = defaultdict(list)
    pattern = re.compile(r'registered (vertex|pixel) shader handle [0-9A-Fa-f]+ hash ([0-9A-Fa-f]+)|shader cache miss for (vertex|pixel) shader ([0-9A-Fa-f]+)')
    logs = []
    for path in args.log:
        raw = path.read_bytes()
        logs.append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest()})
        for number, line in enumerate(raw.decode('utf-8', errors='replace').splitlines(), 1):
            m = pattern.search(line)
            if m:
                stage, value = (m[1], m[2]) if m[1] else (m[3], m[4])
                observations[(stage, f'{int(value,16):016X}')].append({'path': str(path), 'line': number})
    missing = {'vertex': [], 'pixel': []}
    mismatched = []
    for (stage, identity), evidence in sorted(observations.items()):
        if identity not in entries:
            missing[stage].append({'hash': identity, 'evidence': evidence})
        elif entries[identity]['variants']['early']['stage'] != stage:
            mismatched.append({'hash': identity, 'observed_stage': stage, 'evidence': evidence})
    equal_modules = defaultdict(list)
    for identity, entry in entries.items():
        variant = entry['variants']['early']
        equal_modules[(variant['stage'], variant['sha256'])].append(identity)
    summary = {
        'packaged_entries': len(entries),
        'packaged_stages': dict(Counter(e['variants']['early']['stage'] for e in entries.values())),
        'packaged_late_variants': sum('late' in e['variants'] for e in entries.values()),
        'observed_unique_stage_identities': len(observations),
        'observed_stages': dict(Counter(stage for stage, _ in observations)),
        'observation_events': sum(map(len, observations.values())),
        'missing': missing, 'stage_mismatches': mismatched,
        'identical_early_module_groups': [v for v in equal_modules.values() if len(v) > 1],
        'coverage_complete': False,
        'limitation': 'Only supplied logs are compared; no new full-game live corpus was captured. No visual or draw-acceptance claim.',
    }
    args.report.write_text(json.dumps({'summary': summary, 'logs': logs, 'inventory': inventory}, indent=2)+'\n')
    print(json.dumps(summary, indent=2))

if __name__ == '__main__':
    main()
