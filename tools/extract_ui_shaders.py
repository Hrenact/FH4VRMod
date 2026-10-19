"""Extract only DXBC blobs explicitly referenced by a decompiled UI PSO builder."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import re
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('builder', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    raw = (args.manifest.parent / manifest['raw_file']).read_bytes()
    if hashlib.sha256(raw).hexdigest() != manifest['sha256']:
        raise RuntimeError('Image hash mismatch')
    base = manifest['runtime_base']
    addresses = sorted(set(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{12,16})', args.builder.read_text())))
    compiler = c.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/d3dcompiler_47.dll'))
    disassemble = compiler.D3DDisassemble
    disassemble.argtypes = [c.c_void_p, c.c_size_t, c.c_uint, c.c_char_p, c.POINTER(c.c_void_p)]
    disassemble.restype = c.c_long
    out = args.manifest.parent / 'shaders'
    out.mkdir(exist_ok=True)
    index = []
    for address in addresses:
        rva = address - base
        if rva < 0 or rva + 32 > len(raw) or raw[rva:rva+4] != b'DXBC':
            continue
        size, chunks = struct.unpack_from('<II', raw, rva + 24)
        if not (32 <= size <= 1024*1024 and rva + size <= len(raw) and 32+4*chunks <= size):
            raise RuntimeError(f'Invalid DXBC header at {rva:#x}')
        blob = raw[rva:rva+size]
        code = c.create_string_buffer(blob)
        result = c.c_void_p()
        hr = disassemble(code, size, 0, None, c.byref(result))
        if hr < 0 or not result:
            raise RuntimeError(f'D3DDisassemble failed at {rva:#x}: {hr & 0xffffffff:#x}')
        table = c.cast(result, c.POINTER(c.POINTER(c.c_void_p))).contents
        try:
            pointer = c.WINFUNCTYPE(c.c_void_p, c.c_void_p)(table[3])(result)
            length = c.WINFUNCTYPE(c.c_size_t, c.c_void_p)(table[4])(result)
            text = c.string_at(pointer, length).rstrip(b'\0').decode('utf-8')
        finally:
            c.WINFUNCTYPE(c.c_ulong, c.c_void_p)(table[2])(result)
        stem = f'ui_{rva:08x}'
        (out / (stem + '.dxbc')).write_bytes(blob)
        (out / (stem + '.asm.txt')).write_text(text, encoding='utf-8')
        profile = re.search(r'^\s*((?:vs|ps|gs|hs|ds|cs)_\d_\d)', text, re.MULTILINE)
        index.append(dict(rva=hex(rva), size=size, sha256=hashlib.sha256(blob).hexdigest(),
                          profile=profile.group(1) if profile else None, file=stem+'.dxbc'))
    (out / 'index.json').write_text(json.dumps(dict(builder=str(args.builder),
        image_sha256=manifest['sha256'], shaders=index), indent=2), encoding='utf-8')
    print(json.dumps(index, indent=2))


if __name__ == '__main__':
    main()
