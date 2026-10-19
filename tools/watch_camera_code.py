"""Read-only time series of known camera code across scene loading. No injection."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

class Region(c.Structure):
    _fields_=[('BaseAddress',c.c_void_p),('AllocationBase',c.c_void_p),
              ('AllocationProtect',w.DWORD),('PartitionId',w.WORD),('RegionSize',c.c_size_t),
              ('State',w.DWORD),('Protect',w.DWORD),('Type',w.DWORD)]

def capture(pid,base,seconds,interval):
    k=c.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]; k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    k.ReadProcessMemory.restype=w.BOOL
    k.VirtualQueryEx.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(Region),c.c_size_t]; k.VirtualQueryEx.restype=c.c_size_t
    k.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]; k.GetExitCodeProcess.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE]; k.CloseHandle.restype=w.BOOL
    handle=k.OpenProcess(0x1010,False,pid)
    if not handle: raise c.WinError(c.get_last_error())
    def read(address,amount):
        buf=c.create_string_buffer(amount); got=c.c_size_t()
        if not k.ReadProcessMemory(handle,address,buf,amount,c.byref(got)): return b''
        return buf.raw[:got.value]
    samples=[]; changes=[]; previous=None; start=time.monotonic(); exit_code=259
    try:
        while time.monotonic()-start<seconds:
            code=w.DWORD()
            if not k.GetExitCodeProcess(handle,c.byref(code)): raise c.WinError(c.get_last_error())
            exit_code=code.value
            if exit_code!=259: break
            state={}
            for label,rva in [('update',0x327ce40),('view_method',0x3281120)]:
                address=base+rva; page=read(address&~4095,4096); region=Region()
                queried=k.VirtualQueryEx(handle,address,c.byref(region),c.sizeof(region))
                state[label]=dict(rva=hex(rva),bytes=read(address,64).hex(' '),
                                  page_sha256=hashlib.sha256(page).hexdigest() if len(page)==4096 else None,
                                  protect=hex(region.Protect) if queried else None)
            state['vtable_slot12']=read(base+0x55397d0+12*8,8).hex(' ')
            elapsed=time.monotonic()-start
            if state!=previous:
                changes.append(dict(elapsed=elapsed,state=state))
                print(f'Code state recorded at {elapsed:.2f}s (state {len(changes)})',flush=True)
                previous=state
            samples.append(dict(elapsed=elapsed,state_index=len(changes)-1))
            time.sleep(interval)
    finally: k.CloseHandle(handle)
    return dict(pid=pid,base=hex(base),timestamp_utc=datetime.now(timezone.utc).isoformat(),
                samples=samples,changes=changes,exit_code=exit_code,
                caveat='Point-in-time read-only samples. Unchanged samples cannot exclude transient writes between samples.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--base',type=lambda s:int(s,0),required=True)
    p.add_argument('--seconds',type=float,default=120)
    p.add_argument('--interval',type=float,default=0.1)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    if args.seconds<=0 or args.interval<=0: p.error('Duration and interval must be positive')
    report=capture(args.pid,args.base,args.seconds,args.interval)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f"Saved {len(report['samples'])} samples, {len(report['changes'])} states, process exit code {report['exit_code']} to {args.out}")
