"""Read-only UI renderer matrix comparison. Explicit candidate, no process writes."""
import argparse, ctypes as c, json, math, struct, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--pid',type=int,required=True)
p.add_argument('--base',type=lambda x:int(x,0),required=True)
p.add_argument('--address',type=lambda x:int(x,0),required=True)
p.add_argument('--seconds',type=int,default=120)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
if not 1<=a.seconds<=300: p.error('duration outside bounds')
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[c.c_ulong,c.c_bool,c.c_ulong];k.OpenProcess.restype=c.c_void_p
k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x1010,False,a.pid)
if not h:raise c.WinError(c.get_last_error())
rows=[];start=time.monotonic()
try:
    print('UI matrix read-only sampling started',flush=True)
    while time.monotonic()-start<a.seconds:
        buf=c.create_string_buffer(0x428);n=c.c_size_t()
        ok=k.ReadProcessMemory(h,a.address,buf,len(buf),c.byref(n))
        row={'elapsed':time.monotonic()-start,'valid':False}
        if ok and n.value==len(buf) and struct.unpack_from('<Q',buf)[0]==a.base+0x5c7ed70:
            row['valid']=True
            row['matrices']={hex(o):[v if math.isfinite(v) else str(v) for v in struct.unpack_from('<16f',buf,o)] for o in (0x2a0,0x2e0,0x320,0x360,0x3a0,0x3e0)}
            row['flags']=buf.raw[0x420:0x428].hex()
        rows.append(row)
        time.sleep(.1)
finally:
    k.CloseHandle(h)
    a.out.write_text(json.dumps({'pid':a.pid,'address':hex(a.address),'caveat':'Non-atomic UI renderer candidate snapshots; matrices not yet semantically identified.','samples':rows},indent=2),encoding='utf-8')
print(f'Saved {len(rows)} samples; valid={sum(x["valid"] for x in rows)}',flush=True)
