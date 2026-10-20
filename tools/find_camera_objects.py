"""Read-only search for an experimentally identified FH4 camera vtable pointer."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import time


class Region(c.Structure):
    _fields_ = [('BaseAddress', c.c_void_p), ('AllocationBase', c.c_void_p),
                ('AllocationProtect', w.DWORD), ('PartitionId', w.WORD),
                ('RegionSize', c.c_size_t), ('State', w.DWORD),
                ('Protect', w.DWORD), ('Type', w.DWORD)]


def run(args):
    k = c.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    k.ReadProcessMemory.restype = w.BOOL
    k.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(Region), c.c_size_t]
    k.VirtualQueryEx.restype = c.c_size_t
    k.CloseHandle.argtypes = [w.HANDLE]
    k.CloseHandle.restype = w.BOOL
    handle = k.OpenProcess(0x410, False, args.pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    def read(address, size):
        buf = c.create_string_buffer(size)
        got = c.c_size_t()
        ok = k.ReadProcessMemory(handle, address, buf, size, c.byref(got))
        return buf.raw[:got.value] if ok else b''
    started = time.monotonic()
    total = 0
    failed = 0
    matches = []
    complete = False
    target = struct.pack('<Q', args.base + args.vtable_rva)
    try:
        if args.address:
            matches = args.address
            complete = True
        else:
            address = args.start
            while time.monotonic() - started < args.seconds and total < args.max_gib * 1024**3:
                if address >= args.end:
                    complete = True
                    break
                region = Region()
                if not k.VirtualQueryEx(handle, address, c.byref(region), c.sizeof(region)):
                    complete = True
                    break
                region_start = region.BaseAddress or 0
                start = max(address, region_start)
                end = min(args.end, region_start + region.RegionSize)
                if end <= address:
                    break
                address = end
                # Heap objects: committed, private, readable + writable, no guard.
                if region.State != 0x1000 or region.Type != 0x20000 or region.Protect & 0x100 or (region.Protect & 0xff) not in (0x04, 0x08, 0x40, 0x80):
                    continue
                for chunk_start in range(start, end, 1024*1024):
                    if time.monotonic() - started >= args.seconds or total >= args.max_gib * 1024**3:
                        break
                    amount = min(1024*1024, end - chunk_start)
                    data = read(chunk_start, amount)
                    total += amount
                    if not data:
                        failed += 1
                        continue
                    offset = data.find(target)
                    while offset >= 0:
                        if (chunk_start + offset) % 8 == 0:
                            matches.append(chunk_start + offset)
                        offset = data.find(target, offset + 1)
        objects = []
        for address in sorted(set(matches))[:256]:
            data = read(address, 0x380)
            if len(data) != 0x380:
                continue
            objects.append(dict(address=hex(address),
                                vtable_matches=data[:8] == target,
                                bytes_hex=data.hex(),
                                vectors={hex(off): [f if math.isfinite(f) else str(f) for f in struct.unpack_from('<4f', data, off)]
                                         for off in [0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0x260,0x350]}))
        return dict(timestamp_utc=datetime.now(timezone.utc).isoformat(), pid=args.pid,
                    base=hex(args.base), vtable_rva=hex(args.vtable_rva),
                    elapsed_seconds=time.monotonic()-started, bytes_scanned=total,
                    scan_complete=complete, failed_chunks=failed, match_count=len(set(matches)),
                    objects=objects,
                    caveat='Vtable matches are candidate objects, not proof of the active rendering camera. Snapshot is not atomic.')
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid', type=int, required=True)
    p.add_argument('--base', type=lambda s: int(s,0), required=True)
    p.add_argument('--vtable-rva', type=lambda s: int(s,0), default=0x55397d0)
    p.add_argument('--address', type=lambda s: int(s,0), action='append')
    p.add_argument('--seconds', type=int, default=40)
    p.add_argument('--max-gib', type=int, default=8)
    p.add_argument('--start', type=lambda s: int(s,0), default=0)
    p.add_argument('--end', type=lambda s: int(s,0), default=0x7fffffffffff)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--duration', type=float, default=0, help='Repeated snapshots of explicit addresses, in seconds')
    p.add_argument('--interval', type=float, default=0.1)
    args = p.parse_args()
    if args.duration:
        if not args.address or args.duration <= 0 or args.interval <= 0:
            p.error('Continuous sampling requires addresses and positive duration / interval')
        started = time.monotonic()
        samples = []
        print(f'Sampling {len(args.address)} candidates for {args.duration}s', flush=True)
        while time.monotonic() - started < args.duration:
            snapshot = run(args)
            samples.append(dict(elapsed=time.monotonic()-started,
                                timestamp_utc=snapshot['timestamp_utc'], objects=snapshot['objects']))
            time.sleep(args.interval)
        report = dict(pid=args.pid, base=hex(args.base), vtable_rva=hex(args.vtable_rva),
                      samples=samples, sample_count=len(samples))
    else:
        report = run(args)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key:value for key,value in report.items() if key not in ('objects','samples')}, indent=2))
    for obj in report.get('objects', []):
        print(obj['address'], 'vtable_matches=', obj['vtable_matches'])
