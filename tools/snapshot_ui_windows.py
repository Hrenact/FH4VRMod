"""Read-only, bounded snapshots of known FH4 UI globals and window list.

Offsets are from the locally verified image, not a general game-version API.
Pointers are only read; no virtual methods or game iterators are called.
"""
import argparse
import ctypes as c
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--base', type=lambda value: int(value, 0), required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [c.c_ulong, c.c_bool, c.c_ulong]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x1010, False, a.pid)
if not h:
    raise c.WinError(c.get_last_error())


def read(address, size):
    if not address or not 0 < size <= 4096:
        return None
    buf, n = c.create_string_buffer(size), c.c_size_t()
    if k.ReadProcessMemory(h, address, buf, size, c.byref(n)) and n.value == size:
        return buf.raw
    return None


def word(address):
    raw = read(address, 8)
    return struct.unpack('<Q', raw)[0] if raw else 0


def identity(address):
    vt = word(address)
    return {'address': hex(address), 'vtable': hex(vt), 'vtable_rva': hex(vt-a.base) if a.base <= vt < a.base+0x9000000 else None}


result = {'pid': a.pid, 'base': hex(a.base), 'caveat': 'Read-only non-atomic snapshot; no ownership retained. Window kind is not a proven semantic label.'}
try:
    globals_ = {name: word(a.base+rva) for name, rva in (
        ('window_manager', 0x806e340), ('application', 0x80bc278),
        ('application_renderer', 0x80b8860), ('ui_renderer', 0x80cf5c8))}
    result['globals'] = {name: identity(address) for name, address in globals_.items()}
    application = globals_['application']
    manager = word(application+0x48) if application else 0
    result['application_manager'] = identity(manager)
    vt = word(manager)
    result['application_manager']['methods'] = {str(slot): hex(word(vt+slot*8)) for slot in (14, 15)} if vt else {}
    manager = globals_['window_manager']
    if word(manager) != a.base+0x5b9af98:
        raise RuntimeError('Window manager vtable mismatch; refusing list traversal')
    sentinel = word(manager+0xb8)
    node, seen, rows = word(sentinel+8), {sentinel}, []
    while node and node not in seen and len(rows) < 256:
        seen.add(node)
        window = word(node+0x10)
        raw = read(window, 0x280)
        row = {'node': hex(node), **identity(window), 'read_ok': raw is not None}
        if raw:
            row.update(kind=struct.unpack_from('<i', raw, 0x1b8)[0],
                       dimensions=struct.unpack_from('<2f', raw, 0x198),
                       matrix=struct.unpack_from('<16f', raw, 0x1f0),
                       flag26d=raw[0x26d])
        rows.append(row)
        node = word(node+8)
    result.update(windows=rows, list_closed=node == sentinel, limit_reached=len(rows) == 256)
finally:
    k.CloseHandle(h)
a.out.parent.mkdir(parents=True, exist_ok=True)
a.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'globals': result['globals'], 'application_manager': result['application_manager'], 'windows': len(result.get('windows', [])), 'kinds': sorted({r['kind'] for r in result.get('windows', []) if 'kind' in r}), 'list_closed': result.get('list_closed')}))
