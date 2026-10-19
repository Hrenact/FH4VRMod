"""Read one coherent snapshot from the independent head-pose bridge."""
import ctypes as c
import json
import struct
import time
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenFileMappingW.argtypes=[c.c_ulong,c.c_int,c.c_wchar_p]; k.OpenFileMappingW.restype=c.c_void_p
k.MapViewOfFile.argtypes=[c.c_void_p,c.c_ulong,c.c_ulong,c.c_ulong,c.c_size_t]; k.MapViewOfFile.restype=c.c_void_p
k.UnmapViewOfFile.argtypes=[c.c_void_p];k.CloseHandle.argtypes=[c.c_void_p]
k.GetTickCount64.restype=c.c_ulonglong
h=k.OpenFileMappingW(4,False,'Local\\FH4VR_HeadPose_v1')
if not h: raise c.WinError(c.get_last_error())
ptr=k.MapViewOfFile(h,4,0,0,80)
if not ptr:
    error=c.get_last_error(); k.CloseHandle(h);raise c.WinError(error)
try:
    for _ in range(100):
        before=c.c_uint32.from_address(ptr+8).value
        blob=c.string_at(ptr,80)
        after=c.c_uint32.from_address(ptr+8).value
        magic,version,seq,pid,tick,enabled,valid,*values=struct.unpack('<4IQ2I12f',blob)
        if before==after==seq and seq%2==0:
            if magic!=0x31524846 or version!=1: raise ValueError('Wrong protocol')
            print(json.dumps(dict(pid=pid,sequence=seq,age_ms=k.GetTickCount64()-tick,enabled=enabled,
                valid=valid,rotation=values[:9],angles_radians=values[9:]),indent=2));break
        time.sleep(.001)
    else: raise RuntimeError('No coherent snapshot')
finally:
    k.UnmapViewOfFile(ptr);k.CloseHandle(h)
