#!/usr/bin/env python3
"""Summarize the latest launch's sampled guest swaps; NOT display FPS."""
import argparse
import datetime as dt
import json
import re
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('log')
a = p.parse_args()
lines = Path(a.log).read_text(errors='replace').splitlines()
starts = [i for i, s in enumerate(lines) if 'XMA decode scheduling:' in s]
lines = lines[starts[-1]:] if starts else lines
def timestamp(s):
    return dt.datetime.strptime(s[1:24], '%Y-%m-%d %H:%M:%S.%f')
origin = timestamp(lines[0])
frames, audio = [], []
for s in lines:
    if '[FrameFlow]' not in s and '[Theft4Audio]' not in s:
        continue
    row = dict(re.findall(r'(\w+)=([^ ]+)', s))
    row['seconds'] = (timestamp(s) - origin).total_seconds()
    for k, v in list(row.items()):
        if isinstance(v, str) and v.isdigit(): row[k] = int(v)
    (frames if '[FrameFlow]' in s else audio).append(row)
milestones = [f for f in frames if f['swap'] % 300 == 0]
intervals = []
for previous, current in zip(milestones, milestones[1:]):
    duration = current['seconds'] - previous['seconds']
    if duration <= 0: continue
    intervals.append(dict(start=previous['seconds'], end=current['seconds'],
        guest_swaps_per_second=round((current['swap']-previous['swap'])/duration, 2),
        sampled_draws=current.get('draws'), sampled_depth_draws=current.get('depth_only'),
        sampled_resolves=current.get('copies'), outcome=current.get('outcome')))
print(json.dumps(dict(origin=str(origin), caveat='Guest swap throughput, not unique display FPS; draw counts are endpoint samples.',
    intervals=intervals, audio_last=audio[-1] if audio else None,
    pipeline_creation_log_entries=sum('Creating graphics pipeline state' in s for s in lines),
    placeholder_transition_records=sum(f.get('outcome')=='placeholder_skip' for f in frames)), indent=2))
