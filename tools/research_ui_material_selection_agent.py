"""Offline shader selection evidence plus bounded read-only live owner snapshot."""
import ctypes as c
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

root = Path(__file__).resolve().parents[1]
base = 0x7ff70cb10000
data = (root / 'reports/ui-runtime-16224/fh4-image.bin').read_bytes()
out = root / 'reports/ui-material-shaders-agent'
md = Cs(CS_ARCH_X86, CS_MODE_64)
for first, last in [(0x1a62f60, 0x1a631ed), (0x3b96740, 0x3b96810),
                    (0x1a87580, 0x1a87640), (0x1a91e20, 0x1a91ee0),
                    (0x1a91d90, 0x1a91e20)]:
    (out / f'{first:08x}.asm.txt').write_text('\n'.join(
        f'{i.address:08x} {i.bytes.hex():30s} {i.mnemonic} {i.op_str}'
        for i in md.disasm(data[first:last], first)))

table = 0x74391b0 - 0xc9 * 0x20
shaders = []
for index in range(0xc9, 0xef):
    blob, name, size = struct.unpack_from('<QQI', data, table + 0x20 * index)
    offset = name - base
    text = data[offset:data.index(0, offset)].decode()
    shaders.append(dict(index=hex(index), name=text, blob_rva=hex(blob-base), size=size))
(out / 'material-permutation-index.json').write_text(json.dumps(shaders, indent=2))

k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [c.c_ulong, c.c_bool, c.c_ulong]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.ReadProcessMemory.restype = c.c_bool
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x1010, False, 33092)
if not h:
    raise c.WinError(c.get_last_error())
reads = []
def read(address, size):
    assert 0 < size <= 4096
    buf, n = c.create_string_buffer(size), c.c_size_t()
    ok = bool(k.ReadProcessMemory(h, address, buf, size, c.byref(n))) and n.value == size
    reads.append(dict(address=hex(address), size=size, ok=ok))
    if not ok:
        raise RuntimeError(f'Cannot read {address:x} + {size:x}')
    return buf.raw
def qword(address):
    return struct.unpack('<Q', read(address, 8))[0]

record = dict(pid=33092, base=hex(base), caveat='Non-atomic read-only snapshot. No stack packet dereferenced. No game functions called.')
try:
    assert read(base+0x1a7c100, 5) == bytes.fromhex('48895c2408')
    owner_wrapper = qword(base+0x7ee9938)
    owner = qword(owner_wrapper)
    graphics = qword(base+0x80cf5c8)
    ownerbytes, gfxbytes = read(owner, 0x1d0), read(graphics, 0x970)
    record['owner'] = dict(wrapper=hex(owner_wrapper),address=hex(owner), first_word=hex(struct.unpack_from('<Q', ownerbytes)[0]),
        byte1a0=ownerbytes[0x1a0], selected_pipeline1c8=struct.unpack_from('<I', ownerbytes, 0x1c8)[0])
    begin, end = struct.unpack_from('<QQ', gfxbytes, 8)
    record['graphics'] = dict(address=hex(graphics), vtable=hex(struct.unpack_from('<Q', gfxbytes)[0]),
        byte8e1=gfxbytes[0x8e1], stack_begin=hex(begin), stack_end=hex(end), stack_count=(end-begin)//0x70)
    record['type_infos'] = []
    for name,rva in [('FZMaterialPreset',0x7ee8820),('FZMaterialPresetAlphaOnly',0x7ee8970),('FZMaterialPresetTint',0x7eebe40)]:
        raw=read(base+rva,0x32)
        record['type_infos'].append(dict(name=name,rva=hex(rva),bits20=raw[0x20:0x30].hex(),depth=struct.unpack_from('<b',raw,0x30)[0],flags=raw[0x31]))
    bindings = {}
    log = (root/'reports/ui-material-trial-33092/ui-material-probe.log').read_text()
    for line in log.splitlines():
        if not line.startswith('pid=33092 '):
            continue
        fields=dict(re.findall(r'(\w+)=(\S+)', line))
        bindings[fields['preset']] = int(fields['binding'], 16)
    record['bindings'] = []
    for name, ptr in bindings.items():
        raw = read(ptr, 0x108 if name=='Default' else 0x68)
        vt = struct.unpack_from('<Q', raw)[0]
        assert vt == base + {'Default':0x4fe14e0, 'Roundtangle':0x4fe1528}[name]
        offsets = [0x18,0x30,0x48,0x60] + ([0x78,0x90,0xa8,0xc0,0xe8,0x100] if name=='Default' else [])
        record['bindings'].append(dict(name=name,address=hex(ptr),vtable=hex(vt),
            pipeline_ids={hex(off):struct.unpack_from('<I',raw,off)[0] for off in offsets}))
finally:
    k.CloseHandle(h)
record['reads'] = reads
(root / 'reports/ui-material-selection-live-33092-agent.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record,indent=2))
print(json.dumps(shaders,indent=2))
print('Roundtangle compare constant:',struct.unpack_from('<f',data,0x5dc2d3c)[0])
