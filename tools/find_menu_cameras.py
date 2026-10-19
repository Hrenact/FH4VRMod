"""Bounded read-only multi-vtable search; matches are candidates, not active-camera proof."""
import argparse,ctypes as c,json,struct,time
from pathlib import Path
from find_camera_objects import Region
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--base',type=lambda s:int(s,0),required=True);p.add_argument('--start',type=lambda s:int(s,0),default=0);p.add_argument('--end',type=lambda s:int(s,0),default=0x7fffffffffff);p.add_argument('--seconds',type=int,default=45);p.add_argument('--out',required=True);a=p.parse_args()
k=c.WinDLL('kernel32',use_last_error=True);k.OpenProcess.argtypes=[c.c_ulong,c.c_bool,c.c_ulong];k.OpenProcess.restype=c.c_void_p
k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.VirtualQueryEx.argtypes=[c.c_void_p,c.c_void_p,c.POINTER(Region),c.c_size_t];k.VirtualQueryEx.restype=c.c_size_t;k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise c.WinError(c.get_last_error())
def read(address,size):
 b=c.create_string_buffer(size);n=c.c_size_t()
 return b.raw if k.ReadProcessMemory(h,address,b,size,c.byref(n)) and n.value==size else b''
tables={x['name']:int(x['vtable'],0) for report in ('reports/menu-camera-rtti.json','reports/menu-camera-extra-rtti.json','reports/menu-manager-rtti.json') for x in json.loads(Path(report).read_text()) if x.get('object_offset',0)==0}
tables['cockpit-validation']=0x55397d0
needles={n:struct.pack('<Q',a.base+v) for n,v in tables.items()}
started=time.monotonic();cursor=a.start;total=0;matches=[];complete=False
try:
 while time.monotonic()-started<a.seconds:
  if cursor>=a.end:complete=True;break
  r=Region()
  if not k.VirtualQueryEx(h,cursor,c.byref(r),c.sizeof(r)):complete=True;break
  start=max(cursor,r.BaseAddress or 0);end=min(a.end,(r.BaseAddress or 0)+r.RegionSize)
  if end<=cursor:break
  if r.State==0x1000 and r.Type==0x20000 and not r.Protect&0x100 and (r.Protect&255) in (4,8,64,128):
   for at in range(start,end,1024*1024):
    if time.monotonic()-started>=a.seconds:cursor=at;break
    size=min(1024*1024,end-at);data=read(at,size);total+=size
    for name,needle in needles.items():
     off=data.find(needle)
     while off>=0:
      address=at+off
      if address%8==0:
       obj=read(address,0x68);inner=struct.unpack_from('<Q',obj,0x60)[0] if len(obj)==0x68 and (name.startswith('.?AVCUI') or name=='.?AVUIFlowCamera@@') else 0
       inner_data=read(inner,0x10) if inner else b'';vt=struct.unpack_from('<Q',inner_data)[0] if inner_data else 0
       methods=read(vt,53*8) if vt else b''
       matches.append(dict(name=name,address=hex(address),inner=hex(inner),inner_vtable=hex(vt),methods={str(i):hex(struct.unpack_from('<Q',methods,i*8)[0]-a.base) for i in (12,19,49,51,52)} if methods else {}))
      off=data.find(needle,off+1)
   else:cursor=end;continue
   break
  cursor=end
finally:k.CloseHandle(h)
result=dict(pid=a.pid,bytes_scanned=total,seconds=time.monotonic()-started,next_cursor=hex(cursor),complete=complete,candidates=matches)
Path(a.out).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
