"""Summarize finite float changes in candidate camera objects; no semantic claims."""
import argparse
import json
import math
from pathlib import Path
import struct


def summarize(path):
    report = json.loads(path.read_text(encoding='utf-8'))
    by_address = {}
    for sample in report['samples']:
        for obj in sample['objects']:
            if obj['vtable_matches']:
                by_address.setdefault(obj['address'], []).append((sample['elapsed'], bytes.fromhex(obj['bytes_hex'])))
    result = {}
    for address, rows in by_address.items():
        fields = []
        for off in range(8, 0x380, 4):
            values = [struct.unpack_from('<f', data, off)[0] for _, data in rows]
            if not all(math.isfinite(v) and abs(v) < 1e6 for v in values):
                continue
            minimum, maximum = min(values), max(values)
            if maximum - minimum < 0.0001:
                continue
            timeline = []
            last_bucket = -1
            for (elapsed, _), value in zip(rows, values):
                bucket = int(elapsed // 2)
                if bucket != last_bucket:
                    timeline.append([round(elapsed, 2), round(value, 6)])
                    last_bucket = bucket
            fields.append(dict(offset=hex(off), minimum=minimum, maximum=maximum,
                               timeline_2s=timeline))
        result[address] = dict(valid_samples=len(rows), changing_float_candidates=fields)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    result = summarize(args.input)
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    for address, info in result.items():
        print(address, 'valid samples:', info['valid_samples'])
        for field in info['changing_float_candidates']:
            print(field['offset'], f"{field['minimum']:.6g} .. {field['maximum']:.6g}")
