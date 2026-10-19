"""Offline audit artifacts; never opens or writes the game process."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

root = Path(__file__).resolve().parents[1]
source = root / 'reports/ui-runtime-16224'
manifest = json.loads((source / 'manifest.json').read_text())
data = (source / 'fh4-image.bin').read_bytes()
assert hashlib.sha256(data).hexdigest() == manifest['sha256']
rva = 0x56ff2a0
size = struct.unpack_from('<I', data, rva + 24)[0]
blob = data[rva:rva + size]
assert blob[:4] == b'DXBC'
dll = c.WinDLL('d3dcompiler_47.dll')
fn = dll.D3DDisassemble
fn.argtypes = [c.c_void_p,c.c_size_t,c.c_uint,c.c_char_p,c.POINTER(c.c_void_p)]
fn.restype = c.c_long
memory = c.create_string_buffer(blob)
result = c.c_void_p()
assert fn(memory,size,0,None,c.byref(result)) >= 0
vt = c.cast(result,c.POINTER(c.POINTER(c.c_void_p))).contents
try:
    ptr = c.WINFUNCTYPE(c.c_void_p,c.c_void_p)(vt[3])(result)
    length = c.WINFUNCTYPE(c.c_size_t,c.c_void_p)(vt[4])(result)
    text = c.string_at(ptr,length).rstrip(b'\0').decode()
finally:
    c.WINFUNCTYPE(c.c_ulong,c.c_void_p)(vt[2])(result)
out = root / 'reports/ui-material-shaders-agent'
(out / '056ff2a0.dxbc').write_bytes(blob)
(out / '056ff2a0.asm.txt').write_text(text)
input_lines = [line for line in text.splitlines() if line.startswith('dcl_input')]
assert input_lines == ['dcl_input_ps linear v1.z']
assert 'dcl_resource' not in text and 'dcl_sampler' not in text
evidence = dict(image_sha256=manifest['sha256'], shader_rva=hex(rva), size=size,
    shader_sha256=hashlib.sha256(blob).hexdigest(), inputs=input_lines,
    binding_offset='0x60', ps_enum='0xce', common_vs_enum='0x46',
    runtime_changes='None; raw shader is research-only and not required by production gate')
(root / 'reports/ui-coloronly-shader-evidence-agent.json').write_text(json.dumps(evidence,indent=2))

section = next(s for s in manifest['sections'] if s['name']=='.pdata')
entries = struct.iter_unpack('<III',data[section['rva']:section['rva']+section['size']//12*12])
entry = next((a,b,u) for a,b,u in entries if a==0x4150550)
md = Cs(CS_ARCH_X86,CS_MODE_64)
asm='\n'.join(f'{i.address:08x} {i.bytes.hex():30s} {i.mnemonic} {i.op_str}'
    for i in md.disasm(data[entry[0]:entry[1]],entry[0]))
(root/'reports/ui-map-custom-slot31-16224-agent.asm.txt').write_text(asm)
print(json.dumps(evidence,indent=2))
print('Map custom slot31:',hex(entry[0]),hex(entry[1]))
