"""Bounded read-only runtime disassembly, using PE exception function bounds."""
import argparse,ctypes as c,struct,json,bisect,re
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--rvas',nargs='*',default=[]);p.add_argument('--vtable',action='store_true');p.add_argument('--manager',action='store_true');p.add_argument('--fallback-bytes',type=int,default=128,choices=[128,512,2048,4096]);p.add_argument('--linear',action='store_true',help='Read the bounded fallback region across branches; diagnostic data may include adjacent functions');p.add_argument('--out',required=True);a=p.parse_args()
pe=pefile.PE(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4\ForzaHorizon4.exe',fast_load=True)
pe.parse_data_directories(directories=[3]);funcs=[(e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION];starts=[x[0] for x in funcs]
k=c.WinDLL('kernel32',use_last_error=True);k.OpenProcess.restype=c.c_void_p;k.OpenProcess.argtypes=[c.c_ulong,c.c_bool,c.c_ulong];k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x1010,False,a.pid)
def read(rva,size):
    buf=c.create_string_buffer(size);got=c.c_size_t()
    if not k.ReadProcessMemory(h,c.c_void_p(0x7ff70cb10000+rva),buf,size,c.byref(got)) or got.value!=size:raise RuntimeError(hex(rva))
    return buf.raw
assert read(0x32817b0,7)==bytes.fromhex('488b8170020000')
cs=Cs(CS_ARCH_X86,CS_MODE_64)
def body(rva):
    n=bisect.bisect_right(starts,rva)-1
    bounded=n>=0 and funcs[n][0]<=rva<funcs[n][1]
    end=funcs[n][1] if bounded else rva+a.fallback_bytes
    listing=[]
    for i in cs.disasm(read(rva,min(end-rva,16384)),rva):
        listing.append(f'{i.address:08x}: {i.mnemonic} {i.op_str}')
        if not bounded and not a.linear and i.mnemonic in ('ret','jmp'):break
    return listing
result={hex(int(x,0)):body(int(x,0)) for x in a.rvas}
if a.manager:
    result['manager']=[]
    for start,end in funcs:
        if not 0x3140000<=start<0x3160000:continue
        code=body(start)
        hits=[line for line in code if any(f'+ {field}]' in line for field in ('0x370','0x470','0x474','0x478','0x47c'))]
        if hits:result['manager'].append({'rva':hex(start),'hits':hits,'code':code})
    result['leaf_candidates']=[]
    chunk=read(0x3140000,0x20000)
    for match in re.finditer(rb'\xf3\x0f\x10[\x80-\xbf]\x70\x03\x00\x00',chunk):
        rva=0x3140000+match.start()
        result['leaf_candidates'].append({'rva':hex(rva),'code':body(rva)})
if a.vtable:
    result['vtable']=[]
    for slot in range(80,128):
        rva=struct.unpack('<Q',pe.get_data(0x55397d0+8*slot,8))[0]-pe.OPTIONAL_HEADER.ImageBase
        if 0<rva<0x4c00000:result['vtable'].append({'slot':slot,'rva':hex(rva),'code':body(rva)})
Path(a.out).write_text(json.dumps(result,indent=2),encoding='utf-8');k.CloseHandle(h)
print(json.dumps(result,indent=2))

