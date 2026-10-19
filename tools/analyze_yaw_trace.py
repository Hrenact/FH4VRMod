"""Summarize applied yaw telemetry; does not infer physical key states."""
import argparse
import json
import math
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('trace', type=Path)
p.add_argument('--out', type=Path, required=True)
args = p.parse_args()
samples = json.loads(args.trace.read_text(encoding='utf-8'))
if not samples:
    raise ValueError('No samples')
intervals = []
current = None
for sample in samples:
    active = math.isfinite(sample['yaw']) and abs(sample['yaw']) > 0.001
    if active and current is None:
        current = dict(first_active=sample['elapsed'], last_active=sample['elapsed'], samples=0)
    if active:
        current['last_active'] = sample['elapsed']
        current['samples'] += 1
    elif current is not None:
        current['first_inactive_after'] = sample['elapsed']
        current['observed_span_seconds'] = current['last_active'] - current['first_active']
        intervals.append(current)
        current = None
if current is not None:
    current['first_inactive_after'] = None
    current['observed_span_seconds'] = current['last_active'] - current['first_active']
    intervals.append(current)
result = dict(pid=samples[0]['pid'], samples=len(samples),
    elapsed_seconds=samples[-1]['elapsed']-samples[0]['elapsed'],
    modified_outputs=max(s['matched'] for s in samples),
    rejected_outputs=max(s['read_failures'] for s in samples),
    entered=max(s['entered'] for s in samples), returned=max(s['returned'] for s in samples),
    final_yaw=samples[-1]['yaw'],
    max_abs_yaw_degrees=max(abs(s['yaw']) for s in samples)*180/math.pi,
    active_intervals=intervals, final_exit_code=samples[-1]['process_exit_code'],
    caveat='Intervals are sampled applied offsets, not key-down durations. Counters are independent snapshots. Visual effect and recovery require user observation.')
args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
