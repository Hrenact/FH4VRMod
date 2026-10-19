"""Offline-only exact-hash DXBC experiment: make only SV_ClipDistance homogeneous.

Never reads a process or changes an installed game/shader. It consumes a local
export and writes an experimental copy. It does NOT repair native coefficients;
the replay must separately supply full homogeneous inverse plane coefficients.
Token definitions: Microsoft's d3d12TokenizedProgramFormat.hpp in DirectXShaderCompiler.
"""
import argparse
import ctypes as c
import hashlib
import json
import math
from pathlib import Path
import struct

KNOWN = {
    '3a9b9a4db3b5d30e81ddd2dc4e1da2f3a3387d09fa8193b643ee3a1dfec0f3ea': (4, 5),
    'db012a049b5079dea6e0a6da589a992a25418b778ce1a05de2f6b366d0fb993b': (5, 3),
}

def chunks(blob):
    assert blob[:4] == b'DXBC' and struct.unpack_from('<I', blob, 24)[0] == len(blob)
    count = struct.unpack_from('<I', blob, 28)[0]
    offsets = struct.unpack_from('<' + 'I' * count, blob, 32)
    result = []
    for offset in offsets:
        tag, size = struct.unpack_from('<4sI', blob, offset)
        assert offset >= 32 + count * 4 and offset + size + 8 <= len(blob)
        result.append((tag, blob[offset + 8:offset + 8 + size]))
    return result

def build(parts):
    offset = 32 + 4 * len(parts); offsets = []; body = bytearray()
    for tag, content in parts:
        assert offset % 4 == 0 and len(content) % 4 == 0
        offsets.append(offset); body += struct.pack('<4sI', tag, len(content)) + content
        offset += len(content) + 8
    return b'DXBC' + bytes(16) + struct.pack('<III', 1, offset, len(parts)) + struct.pack('<' + 'I' * len(parts), *offsets) + body

def blob_value(pointer):
    table = c.cast(pointer, c.POINTER(c.POINTER(c.c_void_p))).contents
    try:
        address = c.WINFUNCTYPE(c.c_void_p, c.c_void_p)(table[3])(pointer)
        length = c.WINFUNCTYPE(c.c_size_t, c.c_void_p)(table[4])(pointer)
        return c.string_at(address, length)
    finally:
        c.WINFUNCTYPE(c.c_ulong, c.c_void_p)(table[2])(pointer)

def retail_hash(data):
    # MD5 compression with Microsoft's retail-container final-block layout.
    # Algorithm reference: DirectXShaderCompiler/lib/DxilHash/DxilHash.cpp,
    # ComputeHashRetail. This checksum is not a security primitive.
    size = len(data); end = size - size % 64; tail = data[end:]
    blocks = bytearray(data[:end])
    if len(tail) < 56:
        blocks += struct.pack('<I', size << 3) + tail + b'\x80' + bytes(55-len(tail)) + struct.pack('<I', 1 | size << 1)
    else:
        blocks += tail + b'\x80' + bytes(63-len(tail))
        blocks += struct.pack('<I', size << 3) + bytes(56) + struct.pack('<I', 1 | size << 1)
    state = [0x67452301,0xefcdab89,0x98badcfe,0x10325476]
    shifts = [7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21]
    constants = [int(abs(math.sin(i+1))*2**32) for i in range(64)]
    for offset in range(0, len(blocks), 64):
        words = struct.unpack_from('<16I', blocks, offset); a,b,cc,d = state
        for i in range(64):
            if i < 16: f,g=(b&cc)|(~b&d),i
            elif i < 32: f,g=(b&d)|(cc&~d),(5*i+1)%16
            elif i < 48: f,g=b^cc^d,(3*i+5)%16
            else: f,g=cc^(b|~d),(7*i)%16
            value=(a+f+constants[i]+words[g])&0xffffffff; shift=shifts[(i//16)*4+i%4]
            rotated=((value<<shift)|(value>>(32-shift)))&0xffffffff
            a,b,cc,d=d,(b+rotated)&0xffffffff,b,cc
        state=[(x+y)&0xffffffff for x,y in zip(state,[a,b,cc,d])]
    return struct.pack('<4I',*state)

def normalize_container(blob):
    blob=blob[:4]+retail_hash(blob[20:])+blob[20:]
    # Validate the rebuilt container via the documented strip API. No stripping
    # flags means reflection/signatures are retained. It does not fix bad hashes.
    dll = c.WinDLL('d3dcompiler_47.dll')
    fn = dll.D3DStripShader
    fn.argtypes = [c.c_void_p, c.c_size_t, c.c_uint, c.POINTER(c.c_void_p)]
    fn.restype = c.c_long
    memory = c.create_string_buffer(blob); output = c.c_void_p()
    hr = fn(memory, len(blob), 0, c.byref(output))
    if hr < 0:
        raise RuntimeError(f'D3DStripShader rejected experimental container: {hr & 0xffffffff:08x}')
    result = blob_value(output)
    assert result[4:20] != bytes(16)
    return result

def patch(blob):
    digest = hashlib.sha256(blob).hexdigest()
    if digest not in KNOWN:
        raise ValueError('Unknown shader hash; refusing to infer patch locations')
    assert retail_hash(blob[20:]) == blob[4:20], 'Retail checksum implementation must first match the native shader'
    temps, clip_output = KNOWN[digest]
    modified = []; evidence = []; code_count = 0
    for tag, content in chunks(blob):
        if tag not in [b'SHEX', b'SHDR']:
            modified.append((tag, content)); continue
        code_count += 1
        tokens = list(struct.unpack('<' + 'I' * (len(content) // 4), content))
        assert tokens[0] == 0x10050 and tokens[1] == len(tokens)
        output = tokens[:2]; cursor = 2; saves = writes = returns = declarations = 0
        div = [0x0700000e, 0x00100072, 0, 0x00100246, 0, 0x00100ff6, 0]
        while cursor < len(tokens):
            length = (tokens[cursor] >> 24) & 127
            assert length > 0 and not (tokens[cursor] & 0x80000000)
            instruction = tokens[cursor:cursor + length]; opcode = instruction[0] & 2047
            if opcode == 104:
                assert instruction == [0x02000068, temps]
                instruction = [0x02000068, temps + 2]; declarations += 1
            if instruction == div:
                # mov rSaved.x, r0.w, before the existing divide or any dot overwrites w.
                output += [0x05000036, 0x00100012, temps, 0x0010003a, 0]
                saves += 1
            if opcode == 0 and len(instruction) == 9 and instruction[2] == clip_output and (instruction[1] & 0x000ff000) == 0x2000:
                assert saves == 1
                instruction = instruction.copy(); instruction[1] &= ~0x000ff000; instruction[2] = temps + 1
                writes += 1
            if opcode == 62:
                assert saves == 1 and writes == 4 and instruction == [0x0100003e]
                # mul oClip.xyzw, rDistances.xyzw, rSaved.xxxx.
                output += [0x07000038, 0x001020f2, clip_output, 0x00100e46, temps + 1, 0x00100006, temps]
                returns += 1
            output += instruction; cursor += length
        assert declarations == saves == returns == 1 and writes == 4
        output[1] = len(output)
        patched = struct.pack('<' + 'I' * len(output), *output)
        modified.append((tag, patched))
        evidence.append({'code_chunk': tag.decode(), 'old_dwords': len(tokens), 'new_dwords': len(output),
                         'saved_w_temp': temps, 'distance_temp': temps + 1, 'clip_output': clip_output,
                         'only_original_changes': 'dcl_temps count and four clip destination operands'})
    assert code_count == 1
    result = normalize_container(build(modified))
    # Re-serialization must preserve EVERY chunk's edited/unmodified content.
    assert chunks(result) == modified
    return result, {'source_sha256': digest, 'output_sha256': hashlib.sha256(result).hexdigest(),
                    'non_clip_output_arithmetic_unchanged': True, 'evidence': evidence,
                    'limitations': 'Offline experiment; full-inverse plane coefficients also required; not deployed.'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path); parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result, evidence = patch(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_bytes(result)
    args.output.with_suffix('.patch.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence))
