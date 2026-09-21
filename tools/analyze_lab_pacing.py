#!/usr/bin/env python3
"""Summarize a matching Lab pacing/renderer export; never infer displayed FPS."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def read_csv(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError(f'Malformed or truncated CSV: {path.name}')
    return rows


def stats(values):
    values = sorted(float(value) for value in values if value != '')
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError('Timing must be finite and nonnegative')
    if not values:
        return {'count': 0}
    return {'count': len(values), 'mean': statistics.mean(values),
            'p50': values[math.ceil(.5 * len(values)) - 1],
            'p95': values[math.ceil(.95 * len(values)) - 1],
            'p99': values[math.ceil(.99 * len(values)) - 1], 'max': values[-1]}


def analyze(directory):
    meta = json.loads((directory / 'native-performance-meta.json').read_text())
    pacing_meta = meta.get('pacing', {})
    if not meta.get('complete') or not pacing_meta.get('stopped') or not meta.get('worker_transport_split'):
        raise ValueError('Need a completed Lab capture with pacing and worker timing enabled')
    flat = read_csv(directory / 'native-performance-latest.csv')
    pacing = read_csv(directory / 'native-performance-pacing.csv')
    transport = read_csv(directory / 'native-performance-frames.csv')
    if len(flat) != meta['samples'] or len(pacing) != pacing_meta['samples']:
        raise ValueError('Sample counts differ from metadata')
    capture_id = str(meta['capture_id'])
    if any(row['capture_id'] != capture_id for row in flat + pacing):
        raise ValueError('Files belong to different captures')
    if len({row['frame'] for row in flat}) != len(flat):
        raise ValueError('Duplicate renderer frame identities')
    frames = {row['frame']: row for row in flat if not int(row['flags']) & 1}
    if not frames:
        raise ValueError('No complete analyzed frames')
    renderer = [row for row in transport if row['frame'] in frames]
    if len(renderer) != len(frames) or len({row['frame'] for row in renderer}) != len(frames):
        raise ValueError('Missing or duplicate transport frames')
    if any(row['sequence'] != frames[row['frame']]['capture_sequence'] for row in renderer):
        raise ValueError('Transport sequence does not match renderer capture')
    matched = [row for row in pacing if row['frame'] in frames and row['submitted'] == '1']
    sleeps = [row for row in matched if row['wait_requested'] == '1']
    worker_keys = ['worker_queue_mutex_wait_ms', 'worker_condition_wait_ms',
                   'worker_batch_transfer_ms']
    if 'worker_batch_protection_ms' in renderer[0]:
        worker_keys.append('worker_batch_protection_ms')
    worker_keys.extend(('worker_dispatch_ms', 'worker_assembly_ms'))
    limiter_keys = ('present_hook_prepare_ms', 'present_submit_ms', 'limiter_mutex_wait_ms',
                    'decision_wait_ms', 'requested_sleep_ms', 'actual_sleep_ms', 'wake_overshoot_ms',
                    'entry_lateness_ms')
    result = {
        'capture_id': meta['capture_id'], 'renderer_frames': len(frames),
        'matched_pacing_records': len(matched),
        'missing_pacing_frames': sorted(set(frames) - {r['frame'] for r in matched}, key=int),
        'unmatched_pacing_records': len(pacing) - len(matched),
        'dropped_pacing_records': pacing_meta['dropped'],
        'producer_system_threads': sorted({r['system_thread'] for r in matched}),
        'sleep_count': len(sleeps), 'late_resets': sum(r['late_reset'] == '1' for r in matched),
        'limiter_ms': {key: stats(r[key] for r in (sleeps if key in
                        ('requested_sleep_ms', 'actual_sleep_ms', 'wake_overshoot_ms') else matched))
                       for key in limiter_keys},
        'worker_ms': {key: stats(r[key] for r in renderer) for key in worker_keys},
        'worker_partition_errors': sum(int(r['worker_partition_errors']) for r in renderer),
        'renderer_interval_ms': stats(r['cpu_frame_interval_ms'] for r in frames.values()),
        'limits': ['No physical scanout measurement; instrumented elapsed spans include waits.',
                   'Worker condition wait includes queue-mutex reacquisition.',
                   'Worker split partitions the legacy idle total; do not add that total again.',
                   'Frame N interval ends at N+1; use raw ticks and thread identities to correlate.',
                   'Normal limiter sleep is intentional; overshoot is not the requested sleep.',
                   'Missing boundary records and profiling overhead require explicit review.']}
    gap_keys = ('cpu_guest_gap_wall_ms', 'cpu_guest_gap_on_core_ms',
                'cpu_guest_gap_off_core_ms')
    if all(key in flat[0] for key in gap_keys) and 'counter_guest_gap_cpu_valid' in flat[0]:
        valid = [row for row in frames.values() if row['counter_guest_gap_cpu_valid'] == '1']
        if any(abs(float(row[gap_keys[0]]) - float(row[gap_keys[1]]) -
                   float(row[gap_keys[2]])) > .02 for row in valid):
            raise ValueError('Guest gap wall/CPU split does not balance')
        result['guest_gap_cpu_valid_frames'] = len(valid)
        result['guest_gap_cpu_invalid_frames'] = len(frames) - len(valid)
        result['guest_gap_ms'] = {key: stats(row[key] for row in valid) for key in gap_keys}
        result['limits'].append('Legacy guest_gap measures render-worker assembly/dispatch between publications, not guest simulation. Off-core includes waits/descheduling; '
                                'this capture cannot isolate runnable queue delay.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.directory), indent=2) + '\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')


if __name__ == '__main__':
    main()
