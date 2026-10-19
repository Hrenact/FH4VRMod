"""Check candidate row-vector view matrices and summarize observed yaw plateaus."""
import argparse
import json
import math
from pathlib import Path
import struct


def validate(report):
    results = {}
    for sample in report['samples']:
        for obj in sample['objects']:
            if not obj['vtable_matches']:
                continue
            data = bytes.fromhex(obj['bytes_hex'])
            position = struct.unpack_from('<4f', data, 0x160)
            matrix = struct.unpack_from('<16f', data, 0x170)
            yaw = struct.unpack_from('<f', data, 0x2e4)[0]
            result = results.setdefault(obj['address'], dict(samples=0, nonfinite=0,
                orthogonality_max_error=0, translation_max_residual=0,
                affine_column_max_error=0, yaw_min=None, yaw_max=None, yaw_transitions=[]))
            result['samples'] += 1
            if not all(math.isfinite(v) for v in (*position, *matrix, yaw)):
                result['nonfinite'] += 1
                continue
            orthogonal = max(abs(sum(matrix[k*4+i]*matrix[k*4+j] for k in range(3))
                                 - (1 if i == j else 0)) for i in range(3) for j in range(3))
            residual = max(abs(matrix[12+j]+sum(position[k]*matrix[k*4+j] for k in range(3))) for j in range(3))
            affine = max(abs(matrix[3]), abs(matrix[7]), abs(matrix[11]), abs(matrix[15]-1))
            result['orthogonality_max_error'] = max(result['orthogonality_max_error'], orthogonal)
            result['translation_max_residual'] = max(result['translation_max_residual'], residual)
            result['affine_column_max_error'] = max(result['affine_column_max_error'], affine)
            result['yaw_min'] = yaw if result['yaw_min'] is None else min(result['yaw_min'], yaw)
            result['yaw_max'] = yaw if result['yaw_max'] is None else max(result['yaw_max'], yaw)
            bucket = 'positive' if yaw > 1 else 'negative' if yaw < -1 else 'center' if abs(yaw) < 0.05 else 'transition'
            events = result['yaw_transitions']
            if not events or events[-1]['state'] != bucket:
                events.append(dict(elapsed=round(sample['elapsed'],3), state=bucket, yaw=yaw))
    return dict(objects=results, caveat='Non-atomic, read-only object samples. Matrix consistency and yaw changes do not prove final GPU use or safe modification timing.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    result = validate(json.loads(args.input.read_text(encoding='utf-8')))
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
