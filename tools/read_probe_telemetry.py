"""Read camera hook telemetry through a read-only named shared-memory mapping."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True)
p.add_argument('--seconds',type=float,default=15)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--mode',choices=['entry','vtable'],default='entry')
p.add_argument('--slot',type=int,default=12)
p.add_argument('--target-rva',type=lambda x:int(x,0),default=0x3281120)
p.add_argument('--stop-file',type=Path)
args=p.parse_args()
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenFileMappingW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR]; k.OpenFileMappingW.restype=w.HANDLE
k.MapViewOfFile.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,c.c_size_t]; k.MapViewOfFile.restype=c.c_void_p
k.UnmapViewOfFile.argtypes=[c.c_void_p]; k.UnmapViewOfFile.restype=w.BOOL
k.CloseHandle.argtypes=[w.HANDLE]; k.CloseHandle.restype=w.BOOL
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]; k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]; k.ReadProcessMemory.restype=w.BOOL
k.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]; k.GetExitCodeProcess.restype=w.BOOL
handle=k.OpenFileMappingW(4,False,f'Local\\FH4VR_CameraProbe_{args.pid}')
if not handle: raise c.WinError(c.get_last_error())
pointer=k.MapViewOfFile(handle,4,0,0,232)
if not pointer:
    error=c.get_last_error(); k.CloseHandle(handle); raise c.WinError(error)
samples=[]
process=k.OpenProcess(0x1010,False,args.pid)
if not process:
    error=c.get_last_error(); k.UnmapViewOfFile(pointer); k.CloseHandle(handle); raise c.WinError(error)
def read_code(address):
    if not address: return None
    buffer=c.create_string_buffer(32); copied=c.c_size_t()
    if not k.ReadProcessMemory(process,address,buffer,32,c.byref(copied)) or copied.value!=32: return None
    return buffer.raw.hex()
started=time.monotonic()
last_save=started
args.out.parent.mkdir(parents=True,exist_ok=True)
try:
    while time.monotonic()-started<args.seconds:
        if args.stop_file and args.stop_file.exists(): break
        before=c.c_uint32.from_address(pointer).value
        blob=c.string_at(pointer,232)
        after=c.c_uint32.from_address(pointer).value
        if before==after and before%2==0:
            sequence,enabled,pid,status,calls,camera,tick,*floats=struct.unpack_from('<4I3Q6f',blob)
            entered,returned,matched,read_failures,last_entry,target,trampoline,detour=struct.unpack_from('<8Q',blob,64)
            protection,phase=struct.unpack_from('<2I',blob,128)
            if pid!=args.pid: raise ValueError('Telemetry PID mismatch')
            if phase not in (1,2,3,4):
                time.sleep(0.1)
                continue
            exit_code=w.DWORD()
            if not k.GetExitCodeProcess(process,c.byref(exit_code)): raise c.WinError(c.get_last_error())
            observed_target=read_code(target)
            relay_address=None; relay_bytes=None
            if args.mode=='entry' and observed_target and observed_target.startswith('e9'):
                relay_address=target+5+struct.unpack_from('<i',bytes.fromhex(observed_target),1)[0]
                relay_bytes=read_code(relay_address)
            slot_address=target-args.target_rva+0x55397d0+args.slot*8 if args.mode=='vtable' and target else 0
            slot_bytes=read_code(slot_address)
            samples.append(dict(mode=args.mode,slot_address=hex(slot_address) if slot_address else None,
                                observed_slot_pointer=hex(struct.unpack_from('<Q',bytes.fromhex(slot_bytes))[0]) if slot_bytes else None,
                                elapsed=time.monotonic()-started,sequence=sequence,enabled=enabled,pid=pid,
                                status=status,calls=calls,camera=hex(camera),tick=tick,
                                position=floats[:4],fov=floats[4],yaw=floats[5],
                                entered=entered,returned=returned,matched=matched,read_failures=read_failures,
                                last_entry=hex(last_entry),target=hex(target),trampoline=hex(trampoline),detour=hex(detour),
                                protection=hex(protection),phase=phase,original_bytes=blob[136:168].hex(),
                                patched_bytes=blob[168:200].hex(),trampoline_bytes=blob[200:232].hex(),
                                observed_target_bytes=observed_target,observed_trampoline_bytes=read_code(trampoline),
                                observed_relay_address=hex(relay_address) if relay_address else None,observed_relay_bytes=relay_bytes,
                                process_exit_code=exit_code.value))
            if exit_code.value!=259: break
        if time.monotonic()-last_save>=5:
            temporary=args.out.with_suffix('.tmp')
            temporary.write_text(json.dumps(samples,indent=2),encoding='utf-8')
            temporary.replace(args.out);last_save=time.monotonic()
        time.sleep(0.1)
finally:
    k.UnmapViewOfFile(pointer); k.CloseHandle(handle); k.CloseHandle(process)
args.out.parent.mkdir(parents=True,exist_ok=True)
args.out.write_text(json.dumps(samples,indent=2),encoding='utf-8')
print(f'Samples: {len(samples)}')
if samples:
    print(json.dumps(dict(first=samples[0],last=samples[-1]),indent=2))
