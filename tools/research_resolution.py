"""Read-only live executable references to display-mode enumeration; no writes."""
import argparse, bisect, ctypes as c, json, re, struct
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

p = argparse.ArgumentParser()
p.add_argument('--pid', type=int, required=True)
p.add_argument('--base', type=lambda x: int(x, 0), required=True)
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
pe = pefile.PE(str(a.exe))
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [c.c_ulong, c.c_bool, c.c_ulong]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x1010, False, a.pid)
if not h: raise c.WinError(c.get_last_error())
def read(rva, size):
    b = c.create_string_buffer(size); n = c.c_size_t()
    if not k.ReadProcessMemory(h, a.base+rva, b, size, c.byref(n)) or n.value != size:
        raise RuntimeError(f'Cannot read {rva:x}:{size:x}')
    return b.raw
try:
    if read(0, 2) != b'MZ' or read(0x32817b0, 7) != bytes.fromhex('488b8170020000'):
        raise RuntimeError('Unexpected executable/base')
    targets = {i.address-pe.OPTIONAL_HEADER.ImageBase: i.name.decode() for e in pe.DIRECTORY_ENTRY_IMPORT for i in e.imports
               if i.name and b'EnumDisplaySettings' in i.name}
    functions = [(e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
    starts = [f[0] for f in functions]
    refs = []; unreadable = []
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    for s in pe.sections:
        if not s.Characteristics & 0x20000000: continue
        for offset in range(0, s.Misc_VirtualSize, 65536):
            rva = s.VirtualAddress+offset
            try: code = read(rva, min(65536+8,s.Misc_VirtualSize-offset))
            except RuntimeError:
                unreadable.append(hex(rva)); continue
            for m in re.finditer(rb'\xff[\x15\x25]....', code, re.S):
                if m.start() >= 65536: continue
                at = rva+m.start(); target = at+6+struct.unpack_from('<i',m.group(),2)[0]
                if target not in targets: continue
                n = bisect.bisect_right(starts,at)-1
                begin,end = functions[n] if n >= 0 and functions[n][0] <= at < functions[n][1] else (at,at+32)
                body = read(begin,min(end-begin,16384))
                refs.append(dict(reference=hex(at), import_name=targets[target],function=hex(begin),end=hex(end),
                    code=[f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in decoder.disasm(body,begin)]))
    a.out.write_text(json.dumps(dict(pid=a.pid,references=refs,unreadable=unreadable),indent=2),encoding='utf-8')
    print(json.dumps(dict(references=[{k:v for k,v in r.items() if k!='code'} for r in refs],unreadable_chunks=len(unreadable)),indent=2))
finally:
    k.CloseHandle(h)
