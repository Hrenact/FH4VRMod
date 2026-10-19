"""Export only the loaded FH4 image, read-only, for local offline decompilation.

No process writes, suspension, DLL loading, or heap scanning. Unreadable image
pages remain zero in the export and are listed explicitly in the manifest.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import time
import pefile


class Module(c.Structure):
    _fields_ = [('dwSize', w.DWORD), ('th32ModuleID', w.DWORD), ('th32ProcessID', w.DWORD),
                ('GlblcntUsage', w.DWORD), ('ProccntUsage', w.DWORD), ('modBaseAddr', c.c_void_p),
                ('modBaseSize', w.DWORD), ('hModule', w.HMODULE), ('szModule', w.WCHAR * 256),
                ('szExePath', w.WCHAR * 260)]


def export(pid, destination):
    root = Path(__file__).resolve().parents[1]
    seed_rows = json.loads((root / 'reports/ui-rtti-full.json').read_text(encoding='utf-8'))
    seeds = [int(method, 16) for row in seed_rows for method in row['methods']]
    named_seeds = json.loads((root / 'tools/ghidra/ui-seeds.json').read_text(encoding='utf-8'))['functions']
    seeds += [int(item['rva'], 0) for item in named_seeds]
    k = c.WinDLL('kernel32', use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(Module)]
    k.Module32NextW.argtypes = [w.HANDLE, c.POINTER(Module)]
    k.CloseHandle.argtypes = [w.HANDLE]
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    snap = k.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    if snap == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    module = Module(); module.dwSize = c.sizeof(module)
    found = False
    try:
        more = k.Module32FirstW(snap, c.byref(module))
        while more:
            if module.szModule.lower() == 'forzahorizon4.exe':
                found = True
                break
            more = k.Module32NextW(snap, c.byref(module))
    finally:
        k.CloseHandle(snap)
    if not found:
        raise RuntimeError('FH4 module not found')
    path = Path(module.szExePath)
    pe = pefile.PE(str(path), fast_load=True)
    base = module.modBaseAddr
    handle = k.OpenProcess(0x1010, False, pid)
    if not handle:
        raise c.WinError(c.get_last_error())

    def read(rva, length):
        buf = c.create_string_buffer(length); count = c.c_size_t()
        ok = k.ReadProcessMemory(handle, base + rva, buf, length, c.byref(count))
        return buf.raw if ok and count.value == length else None

    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    raw = destination / 'fh4-image.bin'
    if raw.exists():
        k.CloseHandle(handle)
        raise RuntimeError('Refusing to overwrite an existing export')
    started = time.time()
    missing = []
    try:
        checks = {0x3e0b310: '48895c24084889742410', 0x3e0b0c0: '0f10020f118160030000'}
        for rva, expected in checks.items():
            data = read(rva, len(bytes.fromhex(expected)))
            if data != bytes.fromhex(expected):
                raise RuntimeError(f'UI code is unavailable or build differs at {rva:#x}')
        sections = []
        with raw.open('wb') as out:
            out.truncate(pe.OPTIONAL_HEADER.SizeOfImage)
            for index, section in enumerate(pe.sections):
                rva, size = section.VirtualAddress, section.Misc_VirtualSize
                sections.append(dict(index=index, name=section.Name.rstrip(b'\0').decode(),
                    rva=rva, size=size, characteristics=section.Characteristics))
                for start in range(rva, rva + size, 65536):
                    count = min(65536, rva + size - start)
                    data = read(start, count)
                    if data is not None:
                        out.seek(start); out.write(data)
                    else:
                        for page in range(start, start + count, 4096):
                            n = min(4096, start + count - page)
                            data = read(page, n)
                            if data is None:
                                missing.append(dict(rva=page, size=n))
                            else:
                                out.seek(page); out.write(data)
                print(f'Exported {sections[-1]["name"]} RVA={rva:#x} bytes={size}', flush=True)
        # Disk unwind boundaries are hints, not proof that runtime code shares them.
        pe.parse_data_directories(directories=[3])
        boundaries = [[e.struct.BeginAddress, e.struct.EndAddress] for e in pe.DIRECTORY_ENTRY_EXCEPTION]
        with raw.open('rb') as source:
            digest = hashlib.file_digest(source, 'sha256').hexdigest()
        manifest = dict(pid=pid, module=str(path), runtime_base=base, preferred_base=pe.OPTIONAL_HEADER.ImageBase,
            image_size=pe.OPTIONAL_HEADER.SizeOfImage, seconds=time.time()-started,
            raw_file=raw.name, sha256=digest,
            sections=sections, unreadable=missing, seeds=sorted(set(seeds)), named_seeds=named_seeds, unwind_hints=boundaries,
            caveat='Only image bytes, not heap. Missing pages are zero. Runtime lazy decryption may leave other code opaque; validate each function. Installed vtable hooks may point outside this image.')
        (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        print(json.dumps({k: manifest[k] for k in ('pid','runtime_base','seconds','sha256')}, indent=2))
        print(f'Unreadable regions: {len(missing)}')
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    export(args.pid, args.out)
