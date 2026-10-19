"""Read-only PE / running main-module inspection; no injection or memory writes."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from datetime import datetime, timezone


def inspect(path, pid=None, base=None, size=None, disassemble=False, vtable_rva=None):
    disk = path.read_bytes()
    pe = struct.unpack_from('<I', disk, 0x3c)[0]
    if disk[:2] != b'MZ' or disk[pe:pe+4] != b'PE\0\0':
        raise ValueError('Not a PE executable')
    machine, count = struct.unpack_from('<HH', disk, pe + 4)
    optional_size = struct.unpack_from('<H', disk, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', disk, opt)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    image_size = struct.unpack_from('<I', disk, opt + 56)[0]
    sections = []
    for i in range(count):
        off = opt + optional_size + i * 40
        vs, va, raw_size, raw = struct.unpack_from('<IIII', disk, off + 8)
        sections.append(dict(name=disk[off:off+8].rstrip(b'\0').decode('ascii'),
                             rva=va, virtual_size=vs, raw_size=raw_size, raw_offset=raw))
    def rva_at(offset):
        if pid:
            return offset
        for sec in sections:
            if sec['raw_offset'] <= offset < sec['raw_offset'] + sec['raw_size']:
                return sec['rva'] + offset - sec['raw_offset']
        return None

    read_status = None
    if pid:
        if not base or size != image_size:
            raise ValueError('Live base / image size missing or mismatches disk executable')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                            ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.POINTER(ctypes.c_size_t)]
        kernel.ReadProcessMemory.restype = wintypes.BOOL
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = kernel.OpenProcess(0x0010 | 0x1000, False, pid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        data = bytearray(image_size)
        unreadable = []
        total = 0
        try:
            # Small independent reads tolerate inaccessible pages; never dump memory to disk.
            for off in range(0, image_size, 4096):
                amount = min(4096, image_size - off)
                buf = (ctypes.c_char * amount)()
                got = ctypes.c_size_t()
                ok = kernel.ReadProcessMemory(handle, base + off, buf, amount, ctypes.byref(got))
                if ok and got.value == amount:
                    data[off:off+amount] = bytes(buf)
                    total += amount
                else:
                    unreadable.append(off)
        finally:
            kernel.CloseHandle(handle)
        if data[:2] != b'MZ':
            raise ValueError('Cannot read live image header')
        read_status = dict(bytes_read=total, bytes_total=image_size,
                           unreadable_page_count=len(unreadable),
                           unreadable_page_rvas=[hex(x) for x in unreadable])
    else:
        data = disk

    # Public constant-data signatures are research leads, NOT camera pose addresses.
    patterns = {
        'chase_limiter_data': '90 40 CD CC 8C 40 1F 85 2B 3F 00 00 00 40',
        'driver_or_hood_limiter_data': 'CD CC 4C 3E 00 50 43 47 00 00 34 42 00 00 20',
    }
    matches = {}
    for name, pattern in patterns.items():
        matches[name] = [dict(offset=hex(m.start()), rva=hex(rva_at(m.start()))
                              if rva_at(m.start()) is not None else None)
                         for m in re.finditer(re.escape(bytes.fromhex(pattern)), data)]

    strings = []
    for match in re.finditer(rb'[ -~]{6,200}', data):
        value = match.group().decode('ascii')
        if re.search(r'camera|cockpit|projection|viewmatrix', value, re.I):
            rva = rva_at(match.start())
            strings.append(dict(rva=hex(rva) if rva is not None else None, value=value))
    targets = {int(s['rva'], 16): s['value'] for s in strings if s['rva'] and
               re.search(r'CamCockpitOffset|CockpitCam$|INPUTCONTEXT_COCKPITCAM|CockpitDriftCam|Camera.*Fov', s['value'], re.I)}
    references = []
    limiter_rvas = [int(hit['rva'], 16) for group in matches.values() for hit in group if hit['rva']]
    for sec in sections:
        if sec['name'] != '.text':
            continue
        start = sec['rva'] if pid else sec['raw_offset']
        length = sec['virtual_size'] if pid else sec['raw_size']
        code = data[start:start+length]
        # Candidate RIP-relative MOV/LEA encodings. Instruction boundaries are not
        # yet disassembled, so every hit must be verified before use as a hook.
        for match in re.finditer(rb'[\x48\x4c][\x8b\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', code):
            local = match.start()
            if local + 7 > len(code):
                continue
            source_rva = sec['rva'] + local
            target_rva = source_rva + 7 + struct.unpack_from('<i', code, local + 3)[0]
            if target_rva in targets or any(rva - 0x40 <= target_rva < rva + 0x40 for rva in limiter_rvas):
                references.append(dict(instruction_candidate_rva=hex(source_rva),
                                       target_rva=hex(target_rva),
                                       label=targets.get(target_rva, 'candidate limiter data range'),
                                       bytes=bytes(code[local:local+7]).hex(' ')))
    functions = []
    vtable_entries = []
    if disassemble:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / '.deps'))
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        def offset_for_rva(rva):
            if pid:
                return rva
            for sec in sections:
                if sec['rva'] <= rva < sec['rva'] + sec['raw_size']:
                    return sec['raw_offset'] + rva - sec['rva']
            raise ValueError(f'RVA {rva:x} is not file-backed')
        exception_rva, exception_size = struct.unpack_from('<II', disk, opt + 112 + 3 * 8)
        exception_off = offset_for_rva(exception_rva)
        selected = {int(ref['instruction_candidate_rva'], 16) for ref in references
                    if 'limiter' in ref['label'] or 'CamCockpitOffset' in ref['label'] or
                    ref['label'] == 'INPUTCONTEXT_COCKPITCAM'}
        if vtable_rva is not None:
            table_off = offset_for_rva(vtable_rva)
            image_base = base if pid else struct.unpack_from('<Q', disk, opt + 24)[0]
            for slot in range(128):
                ptr = struct.unpack_from('<Q', data, table_off + slot * 8)[0]
                rva = ptr - image_base
                if not any(sec['name'] == '.text' and sec['rva'] <= rva < sec['rva'] + sec['virtual_size'] for sec in sections):
                    break
                vtable_entries.append(dict(slot=slot, function_rva=hex(rva)))
                selected.add(rva)
            # Narrow research neighborhood around the identified camera methods.
            # Find direct MOVUPS/MOVAPS stores with displacement +0x170, then
            # disassemble containing ranges to reject false instruction boundaries.
            text_section = next(sec for sec in sections if sec['name'] == '.text')
            code_off = offset_for_rva(text_section['rva'])
            code = data[code_off:code_off + text_section['virtual_size']]
            for match in re.finditer(rb'\x0f[\x11\x29][\x80-\xbf]\x70\x01\x00\x00', code):
                rva = text_section['rva'] + match.start()
                if 0x3270000 <= rva < 0x32a0000:
                    selected.add(rva)
            for match in re.finditer(rb'[\x48-\x4f]\x8d[\x80-\xbf]\x70\x01\x00\x00', code):
                rva = text_section['rva'] + match.start()
                if 0x3270000 <= rva < 0x32a0000:
                    selected.add(rva)
        for off in range(exception_off, exception_off + exception_size - 11, 12):
            begin, end, unwind = struct.unpack_from('<III', data, off)
            hits = sorted(rva for rva in selected if begin <= rva < end)
            if not hits or end <= begin or end - begin > 0x10000:
                continue
            body_offset = offset_for_rva(begin)
            listing = [dict(rva=hex(ins.address), asm=f'{ins.mnemonic} {ins.op_str}')
                       for ins in decoder.disasm(bytes(data[body_offset:body_offset+end-begin]), begin)]
            boundaries = {int(ins['rva'], 16) for ins in listing}
            functions.append(dict(begin=hex(begin), end=hex(end),
                                  confirmed_reference_boundaries=[hex(x) for x in hits if x in boundaries],
                                  instructions=listing))
    return dict(timestamp_utc=datetime.now(timezone.utc).isoformat(),
                executable=str(path), sha256=hashlib.sha256(disk).hexdigest(),
                machine=hex(machine), image_size=image_size, sections=sections,
                source='live_main_module' if pid else 'disk', pid=pid,
                live_base=hex(base) if base else None, read_status=read_status,
                signature_matches=matches, camera_related_strings=strings,
                candidate_code_references=references,
                candidate_functions=functions,
                vtable_entries=vtable_entries,
                caveat='Limiter constants and strings are unverified research leads, not live camera matrices.',
                signature_reference='https://github.com/ForzaMods/Forza-Mods-AIO/blob/447030847c136d0ef9fcc3c2d7ded2c0d13eb5bd/Forza-Mods-AIO/Forza-Mods-AIO/Cheats/ForzaHorizon4/CameraCheats.cs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--base', type=lambda x: int(x, 0))
    parser.add_argument('--size', type=int)
    parser.add_argument('--disassemble', action='store_true')
    parser.add_argument('--vtable-rva', type=lambda x: int(x, 0))
    args = parser.parse_args()
    report = inspect(args.exe, args.pid, args.base, args.size, args.disassemble, args.vtable_rva)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({key: report[key] for key in
                      ['source', 'sha256', 'image_size', 'signature_matches']}, indent=2))
    print(f'Camera-related strings: {len(report["camera_related_strings"])}')
    print(f'Report: {args.out.resolve()}')
