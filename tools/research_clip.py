"""Read-only disk camera vtable and clipping string research."""
import re,struct,json,bisect
from pathlib import Path
import pefile
import ctypes as c
import argparse
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
path=Path(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4\ForzaHorizon4.exe')
pe=pefile.PE(str(path));data=path.read_bytes();base=pe.OPTIONAL_HEADER.ImageBase
decoder=Cs(CS_ARCH_X86,CS_MODE_64)
parser=argparse.ArgumentParser();parser.add_argument('--pid',type=int);args=parser.parse_args()
handle=None
if args.pid:
    k=c.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[c.c_ulong,c.c_bool,c.c_ulong];k.OpenProcess.restype=c.c_void_p
    k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    k.CloseHandle.argtypes=[c.c_void_p]
    handle=k.OpenProcess(0x1010,False,args.pid)
    if not handle:raise c.WinError(c.get_last_error())
def read(rva,size):
    if not handle:return pe.get_data(rva,size)
    buf=c.create_string_buffer(size);got=c.c_size_t()
    if not k.ReadProcessMemory(handle,c.c_void_p(0x7ff70cb10000+rva),buf,size,c.byref(got)) or got.value!=size:raise RuntimeError(f'read failed {rva:x}')
    return buf.raw
if handle and read(0x32817b0,7)!=bytes.fromhex('488b8170020000'):raise RuntimeError('live base/prologue mismatch')
funcs=[(e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
starts=[a for a,b in funcs]
def body(rva):
    n=bisect.bisect_right(starts,rva)-1
    end=funcs[n][1] if n>=0 and funcs[n][0]<=rva<funcs[n][1] else rva+96
    return [f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in decoder.disasm(read(rva,min(end-rva,4096)),rva)]
strings=[]
for m in re.finditer(rb'[ -~]{5,}',data):
    if re.search(rb'near.?clip|far.?clip|clip.?plane|near.?plane|far.?plane',m.group(),re.I):
        strings.append({'rva':hex(pe.get_rva_from_offset(m.start())),'text':m.group().decode()})
vtable=[]
for slot in range(80):
    rva=struct.unpack('<Q',pe.get_data(0x55397d0+8*slot,8))[0]-base
    if 0<rva<pe.OPTIONAL_HEADER.SizeOfImage:
        vtable.append({'slot':slot,'rva':hex(rva),'code':body(rva)})
refs=[]
if handle:
    targets={int(s['rva'],16) for s in strings if s['text'] in ('NearClipFOV0','NearClipFOV1','NearClip0','NearClip1','CamNearClip','CamFarClip')}
    targets.update((0x73d369c,0x73d36d4,0x73d36f8,0x73d36fc,0x74320b0,0x74320c0))
    for section in pe.sections:
        if not section.Characteristics&0x20000000:continue
        start=section.VirtualAddress;code=read(start,section.Misc_VirtualSize)
        for match in re.finditer(rb'(?:[\x48\x4c]\x8d|\xf3\x0f[\x10\x58\x59\x5c\x5d\x5e\x5f])[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',code,re.S):
            address=start+match.start();target=address+len(match.group())+struct.unpack_from('<i',match.group(),len(match.group())-4)[0]
            if target in targets:
                n=bisect.bisect_right(starts,address)-1
                refs.append({'reference':hex(address),'target':hex(target),'function':hex(starts[n]),'code':body(starts[n])})
report={'strings':strings,'vtable':vtable,'references':refs,'source':'live' if handle else 'disk','caveat':'Unverified research leads; disk code may be packed; no process writes.'}
Path('reports/clip-research'+('-live' if handle else '')+'.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps([s for s in strings if int(s['rva'],16)<0x55bc000],indent=2))
print('refs',[(r['reference'],r['target'],r['function']) for r in refs])
for item in vtable:
    if len(item['code'])<18:print(item)
if handle:k.CloseHandle(handle)

