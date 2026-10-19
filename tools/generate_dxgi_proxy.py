"""Generate ABI-preserving x64 forwarding stubs for locally observed DXGI exports."""
from pathlib import Path
exports = [
    ('ApplyCompatResolutionQuirking',1),('CompatString',2),('CompatValue',3),
    ('CreateDXGIFactory',10),('CreateDXGIFactory1',11),('CreateDXGIFactory2',12),
    ('DXGID3D10CreateDevice',13),('DXGID3D10CreateLayeredDevice',14),
    ('DXGID3D10GetLayeredDeviceSize',15),('DXGID3D10RegisterLayers',16),
    ('DXGIDeclareAdapterRemovalSupport',17),('DXGIDisableVBlankVirtualization',18),
    ('DXGIDumpJournal',4),('DXGIGetDebugInterface1',19),('DXGIReportAdapterConfiguration',20),
    ('PIXBeginCapture',5),('PIXEndCapture',6),('PIXGetCaptureState',7),
    ('SetAppCompatStringPointer',8),('UpdateHMDEmulationStatus',9),
]
root = Path(__file__).resolve().parent.parent/'src'
lines = ['option casemap:none','EXTERN ResolveDxgi:PROC','.code']
for index,(name,ordinal) in enumerate(exports):
    lines += [f'PUBLIC Proxy_{name}',f'Proxy_{name} PROC FRAME',
              'sub rsp, 0C8h','.allocstack 0C8h','.endprolog',
              'mov [rsp+20h], rcx','mov [rsp+28h], rdx',
              'mov [rsp+30h], r8','mov [rsp+38h], r9']
    lines += [f'movdqu [rsp+{0x40+i*16:X}h], xmm{i}' for i in range(6)]
    lines += [f'mov ecx, {index}','call ResolveDxgi']
    lines += [f'movdqu xmm{i}, [rsp+{0x40+i*16:X}h]' for i in range(6)]
    lines += ['mov rcx, [rsp+20h]','mov rdx, [rsp+28h]',
              'mov r8, [rsp+30h]','mov r9, [rsp+38h]',
              'add rsp, 0C8h','jmp rax',f'Proxy_{name} ENDP']
lines += ['END']
(root/'dxgi_forwarders.asm').write_text('\n'.join(lines)+'\n')
(root/'dxgi_proxy.def').write_text('LIBRARY dxgi\nEXPORTS\n'+''.join(f'    {name}=Proxy_{name} @{ordinal}\n' for name,ordinal in exports))
(root/'dxgi_exports.inc').write_text('\n'.join(f'    "{name}",' for name,_ in exports)+'\n')
