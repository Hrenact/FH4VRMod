"""Read-only offline evidence for FH4 material packet and embedded shader contracts.

Reads the preserved 16224 image, writes only reports/ui-material-*-agent files.
Does not open the game process or Ghidra project.
"""
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / 'reports/ui-runtime-16224'
    manifest = json.loads((source / 'manifest.json').read_text())
    data = (source / 'fh4-image.bin').read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == manifest['sha256']
    base = manifest['runtime_base']
    out = root / 'reports/ui-material-shaders-agent'
    out.mkdir(exist_ok=True)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    ranges = [(0x1a81070, 0x1a8137d), (0x1a7bd70, 0x1a7c0fd),
              (0x1a6ee80, 0x1a6f01b), (0x1a61530, 0x1a6253e),
              (0x1a6a970, 0x1a6ab14)]
    for first, last in ranges:
        text = '\n'.join(f'{i.address:08x} {i.bytes.hex():30s} {i.mnemonic} {i.op_str}'
                         for i in md.disasm(data[first:last], first))
        (out / f'{first:08x}.asm.txt').write_text(text + '\n')

    compiler = c.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/d3dcompiler_47.dll'))
    disassemble = compiler.D3DDisassemble
    disassemble.argtypes = [c.c_void_p, c.c_size_t, c.c_uint, c.c_char_p, c.POINTER(c.c_void_p)]
    disassemble.restype = c.c_long
    shaders = []
    for rva in [0x5a6a0c0, 0x5a6aff0, 0x5b113c0]:
        assert data[rva:rva + 4] == b'DXBC'
        size = struct.unpack_from('<I', data, rva + 24)[0]
        blob = data[rva:rva + size]
        memory = c.create_string_buffer(blob)
        result = c.c_void_p()
        hr = disassemble(memory, size, 0, None, c.byref(result))
        assert hr >= 0 and result
        table = c.cast(result, c.POINTER(c.POINTER(c.c_void_p))).contents
        try:
            ptr = c.WINFUNCTYPE(c.c_void_p, c.c_void_p)(table[3])(result)
            length = c.WINFUNCTYPE(c.c_size_t, c.c_void_p)(table[4])(result)
            text = c.string_at(ptr, length).rstrip(b'\0').decode('utf-8')
        finally:
            c.WINFUNCTYPE(c.c_ulong, c.c_void_p)(table[2])(result)
        stem = f'{rva:08x}'
        (out / (stem + '.dxbc')).write_bytes(blob)
        (out / (stem + '.asm.txt')).write_text(text)
        shaders.append(dict(rva=hex(rva), size=size, sha256=hashlib.sha256(blob).hexdigest(),
                            divides_w_before_clip='div r0.xyz, r0.xyzx, r0.wwww' in text))

    table_base = 0x7436a30
    for index in [0x46, 0x47]:
        entry = table_base + index * 0x20
        blob, name, size = struct.unpack_from('<QQI', data, entry)
        string_at = name - base
        string = data[string_at:data.index(0, string_at)].decode()
        shaders[index - 0x46]['registration'] = dict(entry=hex(entry), enum=hex(index),
                                                    name=string, blob=hex(blob - base), size=size)

    colptr = struct.unpack_from('<Q', data, 0x4fe0208)[0] - base
    signature, displacement, cd, typedesc, hierarchy, self_rva = struct.unpack_from('<IIIIII', data, colptr)
    assert signature == 1 and self_rva == colptr
    name = data[typedesc + 16:data.index(0, typedesc + 16)].decode()
    assert struct.unpack_from('<Q', data, 0x4fe0210 + 31 * 8)[0] == base + 0x1a81070
    record = dict(image_sha256=digest, base=hex(base),
                  producer=dict(rva='0x1a81070', vtable='0x4fe0210', slot=31, rtti=name,
                                abi='void(element*, context*)'),
                  packet_builder=dict(rva='0x1a7bd70', plane_argument=7,
                                      packet_plane_offsets=['0x50', '0x60', '0x70', '0x80'],
                                      dispatch_site='0x1a7c0a9'), shaders=shaders)
    (root / 'reports/ui-material-packet-evidence-agent.json').write_text(json.dumps(record, indent=2))
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
