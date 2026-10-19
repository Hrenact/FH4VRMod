"""Read-only FOV and foreground sampling for a validated camera in one process."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import math
from pathlib import Path
import struct
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--camera', type=lambda x: int(x, 0), required=True)
p.add_argument('--base', type=lambda x: int(x, 0), required=True)
p.add_argument('--seconds', type=float, default=60)
p.add_argument('--out', type=Path, required=True)
args = p.parse_args()
k = c.WinDLL('kernel32', use_last_error=True)
u = c.WinDLL('user32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.ReadProcessMemory.restype = w.BOOL
k.CloseHandle.argtypes = [w.HANDLE]
u.GetForegroundWindow.restype = w.HWND
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
h = k.OpenProcess(0x1010, False, args.pid)
if not h:
    raise c.WinError(c.get_last_error())

def read(address, count):
    buf = c.create_string_buffer(count)
    got = c.c_size_t()
    if not k.ReadProcessMemory(h, address, buf, count, c.byref(got)) or got.value != count:
        raise c.WinError(c.get_last_error())
    return buf.raw

samples = []
start = time.monotonic()
try:
    while time.monotonic() - start < args.seconds:
        data = read(args.camera, 0x330)
        if struct.unpack_from('<Q', data)[0] != args.base + 0x55397d0:
            raise RuntimeError('Camera vtable mismatch; stop sampling')
        fov = struct.unpack_from('<f', data, 0x1b0)[0]
        settings_ptr = struct.unpack_from('<Q', data, 0x328)[0]
        settings = read(settings_ptr + 0x18, 12)
        foreground = w.DWORD()
        u.GetWindowThreadProcessId(u.GetForegroundWindow(), c.byref(foreground))
        samples.append(dict(elapsed=time.monotonic()-start,
                            foreground=foreground.value == args.pid,
                            fov=fov if math.isfinite(fov) else None,
                            degrees=math.degrees(fov) if math.isfinite(fov) else None,
                            flag=data[0x320], settings=list(struct.unpack('<3f', settings))))
        time.sleep(0.1)
finally:
    k.CloseHandle(h)
    args.out.write_text(json.dumps(dict(pid=args.pid, camera=hex(args.camera), samples=samples), indent=2))
print('Samples:', len(samples))
for foreground in (False, True):
    values = [s['degrees'] for s in samples if s['foreground'] == foreground and s['degrees'] is not None]
    if values:
        print('foreground=', foreground, 'count=', len(values), 'degrees_range=', (min(values), max(values)))
