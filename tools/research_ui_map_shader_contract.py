"""Offline DXBC/RDEF scan for the map renderer's 0xd0 constant block."""
from pathlib import Path
import ctypes as c
import hashlib
import json
import struct

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'reports/ui-runtime-16224'
OUT=ROOT/'reports/ui-map-shaders-41736'
OUT.mkdir(exist_ok=True)
manifest=json.loads((SRC/'manifest.json').read_text())
data=(SRC/'fh4-image.bin').read_bytes()
assert hashlib.sha256(data).hexdigest()==manifest['sha256']
compiler=c.WinDLL('d3dcompiler_47.dll')
fn=compiler.D3DDisassemble
fn.argtypes=[c.c_void_p,c.c_size_t,c.c_uint,c.c_char_p,c.POINTER(c.c_void_p)]
fn.restype=c.c_long
def string(b,o):
    if not 0<=o<len(b): return ''
    return b[o:b.find(b'\0',o)].decode('ascii','replace')
def disasm(blob):
    memory=c.create_string_buffer(blob)
    result=c.c_void_p()
    if fn(memory,len(blob),0,None,c.byref(result))<0: return None
    vt=c.cast(result,c.POINTER(c.POINTER(c.c_void_p))).contents
    try:
        ptr=c.WINFUNCTYPE(c.c_void_p,c.c_void_p)(vt[3])(result)
        n=c.WINFUNCTYPE(c.c_size_t,c.c_void_p)(vt[4])(result)
        return c.string_at(ptr,n).rstrip(b'\0').decode()
    finally: c.WINFUNCTYPE(c.c_ulong,c.c_void_p)(vt[2])(result)

index=[]
pos=0
while True:
    pos=data.find(b'DXBC',pos)
    if pos<0: break
    base=pos
    pos+=4
    if base+32>len(data): continue
    size,n=struct.unpack_from('<II',data,base+24)
    if not 32<=size<=1000000 or n>40 or base+size>len(data): continue
    blob=data[base:base+size]
    if 32+4*n>size: continue
    cb=[]; profile=None
    for offset in struct.unpack_from('<'+'I'*n,blob,32):
        if offset+8>size: continue
        tag=blob[offset:offset+4]
        length=struct.unpack_from('<I',blob,offset+4)[0]
        chunk=blob[offset+8:offset+8+length]
        if tag in (b'SHDR',b'SHEX') and len(chunk)>=4:
            token=struct.unpack_from('<I',chunk)[0]
            profile=(token>>16,token&0xffff)
        if tag!=b'RDEF' or len(chunk)<28: continue
        count,start=struct.unpack_from('<II',chunk)
        if count>100 or start+24*count>len(chunk): continue
        for k in range(count):
            name,nvars,varoffset,cbsize,flags,typ=struct.unpack_from('<6I',chunk,start+24*k)
            if cbsize!=0xd0: continue
            cb.append(dict(name=string(chunk,name),size=cbsize,variables=nvars))
    if not cb: continue
    asm=disasm(blob)
    if not asm: continue
    stem=f'{base:08x}'
    (OUT/(stem+'.dxbc')).write_bytes(blob)
    (OUT/(stem+'.asm.txt')).write_text(asm)
    index.append(dict(rva=hex(base),size=size,profile=profile,constant_buffers=cb,
                      sha256=hashlib.sha256(blob).hexdigest()))
for record in index:
    record['registrations']=[]
    for enum in range(512):
        entry=0x7436a30+enum*0x20
        ptr,name,size=struct.unpack_from('<QQI',data,entry)
        if ptr!=manifest['runtime_base']+int(record['rva'],16): continue
        name_rva=name-manifest['runtime_base']
        record['registrations'].append(dict(enum=hex(enum),entry=hex(entry),
            name=string(data,name_rva),size=size))
(OUT/'index.json').write_text(json.dumps(index,indent=2))
print(json.dumps(index,indent=2))
