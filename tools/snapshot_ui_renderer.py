"""Bounded read-only UI state snapshots from a caller-supplied renderer address."""
import argparse
import ctypes as c
import json
import math
from pathlib import Path
import struct
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--base', type=lambda x: int(x, 0), required=True)
p.add_argument('--address', type=lambda x: int(x, 0), required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--samples', type=int, default=30)
a = p.parse_args()
if not 1 <= a.samples <= 1000:
    p.error('samples must be 1..1000')
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [c.c_ulong, c.c_bool, c.c_ulong]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x1010, False, a.pid)
if not h:
    raise c.WinError(c.get_last_error())

def read(address, size):
    buf = c.create_string_buffer(size)
    n = c.c_size_t()
    if k.ReadProcessMemory(h, address, buf, size, c.byref(n)) and n.value == size:
        return buf.raw
    return None

def floats(buf, offset, count=16):
    return [x if math.isfinite(x) else str(x) for x in struct.unpack_from('<' + 'f' * count, buf, offset)]

rows = []
try:
    for _ in range(a.samples):
        buf = read(a.address, 0x660)
        row = {'time': time.time(), 'valid': bool(buf and struct.unpack_from('<Q', buf)[0] == a.base + 0x5c7ed70)}
        if row['valid']:
            row['matrices'] = {hex(o): floats(buf, o) for o in (0x2a0, 0x2e0, 0x320, 0x360, 0x3a0, 0x3e0, 0x430, 0x470, 0x4e0, 0x520, 0x5b0)}
            row['viewport'] = floats(buf, 0x78, 4)
            row['scissor'] = list(struct.unpack_from('<4i', buf, 0x90))
            row['target'] = hex(struct.unpack_from('<Q', buf, 0x70)[0])
            row['layer'] = hex(struct.unpack_from('<Q', buf, 0x650)[0])
            row['cache_valid'] = buf[0x420]
            data, capacity, count = struct.unpack_from('<QII', buf, 0x270)
            row['clip_stack'] = {'data': hex(data), 'capacity': capacity, 'count': count}
            if data and 0 < count <= min(capacity, 4096):
                top = read(data + (count - 1) * 20, 20)
                if top:
                    row['clip_stack'].update(rect=floats(top, 0, 4), enabled=top[16], padding=top[17:].hex())
        rows.append(row)
        time.sleep(.01)
finally:
    k.CloseHandle(h)
a.out.parent.mkdir(parents=True, exist_ok=True)
a.out.write_text(json.dumps({'pid': a.pid, 'address': hex(a.address), 'caveat': 'Read-only, non-atomic snapshots. Stale caches and cross-field races are possible.', 'samples': rows}, indent=2), encoding='utf-8')
print(f'Saved {len(rows)} samples, {sum(row["valid"] for row in rows)} valid')
