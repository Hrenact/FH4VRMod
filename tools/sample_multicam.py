"""Read-only candidate camera snapshots; no claim of active-render coverage."""
import argparse,ctypes as c,json,struct,time,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--base',type=lambda v:int(v,0),required=True);p.add_argument('--multicam',type=lambda v:int(v,0),required=True);p.add_argument('--ui',type=lambda v:int(v,0),required=True);p.add_argument('--cockpit',type=lambda v:int(v,0),required=True);p.add_argument('--seconds',type=int,default=600);p.add_argument('--out',type=Path,required=True);p.add_argument('--stop-file',type=Path,required=True);a=p.parse_args()
k=c.WinDLL('kernel32',use_last_error=True);k.OpenProcess.argtypes=[c.c_ulong,c.c_bool,c.c_ulong];k.OpenProcess.restype=c.c_void_p;k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise c.WinError(c.get_last_error())
def read(at,size):
 b=c.create_string_buffer(size);n=c.c_size_t();return b.raw if k.ReadProcessMemory(h,at,b,size,c.byref(n)) and n.value==size else b''
def numbers(data,fmt,offset):
 return [v if math.isfinite(v) else str(v) for v in struct.unpack_from(fmt,data,offset)]
def refs(data):
 result={}
 for off in range(8,len(data)-7,8):
  ptr=struct.unpack_from('<Q',data,off)[0]
  if ptr<0x10000 or ptr>=0x7fffffffffff or ptr%8:continue
  raw=read(ptr,8)
  if not raw:continue
  vt=struct.unpack('<Q',raw)[0]
  if not a.base+0x4c00000<=vt<a.base+0x7000000:continue
  result[hex(off)]={'pointer':hex(ptr),'vtable_rva':hex(vt-a.base)}
 return result
rows=[];start=time.monotonic();last_save=0
def save():
 tmp=a.out.with_suffix('.tmp');tmp.write_text(json.dumps({'pid':a.pid,'caveat':'Non-atomic read-only candidate snapshots','samples':rows},indent=2));tmp.replace(a.out)
print('Read-only multicamera sampling active; saves every 5 seconds',flush=True)
try:
 while time.monotonic()-start<a.seconds and not a.stop_file.exists():
  row={'elapsed':time.monotonic()-start}
  for name,address,size,vt in [('multicam',a.multicam,0x500,0x5306aa0),('ui',a.ui,0x100,0x4f14878),('cockpit',a.cockpit,0x390,0x55397d0)]:
   data=read(address,size);valid=bool(data) and struct.unpack_from('<Q',data)[0]==a.base+vt
   obj={'address':hex(address),'valid':valid};row[name]=obj
   if not valid:continue
   if name=='multicam':obj.update(matrix=numbers(data,'<16f',0x330),fov=numbers(data,'<f',0x370),position=numbers(data,'<4d',0x378),refs=refs(data[:0x330]))
   elif name=='ui':obj['refs']=refs(data)
   else:obj.update(position=numbers(data,'<3d',0x270))
  rows.append(row)
  if time.monotonic()-start-last_save>=5:save();last_save=time.monotonic()-start
  time.sleep(.2)
finally:
 k.CloseHandle(h);save()
print(f'Saved {len(rows)} snapshots',flush=True)
