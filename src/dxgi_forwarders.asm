option casemap:none
EXTERN ResolveDxgi:PROC
.code
PUBLIC Proxy_ApplyCompatResolutionQuirking
Proxy_ApplyCompatResolutionQuirking PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 0
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_ApplyCompatResolutionQuirking ENDP
PUBLIC Proxy_CompatString
Proxy_CompatString PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 1
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_CompatString ENDP
PUBLIC Proxy_CompatValue
Proxy_CompatValue PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 2
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_CompatValue ENDP
PUBLIC Proxy_CreateDXGIFactory
Proxy_CreateDXGIFactory PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 3
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_CreateDXGIFactory ENDP
PUBLIC Proxy_CreateDXGIFactory1
Proxy_CreateDXGIFactory1 PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 4
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_CreateDXGIFactory1 ENDP
PUBLIC Proxy_CreateDXGIFactory2
Proxy_CreateDXGIFactory2 PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 5
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_CreateDXGIFactory2 ENDP
PUBLIC Proxy_DXGID3D10CreateDevice
Proxy_DXGID3D10CreateDevice PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 6
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGID3D10CreateDevice ENDP
PUBLIC Proxy_DXGID3D10CreateLayeredDevice
Proxy_DXGID3D10CreateLayeredDevice PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 7
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGID3D10CreateLayeredDevice ENDP
PUBLIC Proxy_DXGID3D10GetLayeredDeviceSize
Proxy_DXGID3D10GetLayeredDeviceSize PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 8
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGID3D10GetLayeredDeviceSize ENDP
PUBLIC Proxy_DXGID3D10RegisterLayers
Proxy_DXGID3D10RegisterLayers PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 9
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGID3D10RegisterLayers ENDP
PUBLIC Proxy_DXGIDeclareAdapterRemovalSupport
Proxy_DXGIDeclareAdapterRemovalSupport PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 10
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGIDeclareAdapterRemovalSupport ENDP
PUBLIC Proxy_DXGIDisableVBlankVirtualization
Proxy_DXGIDisableVBlankVirtualization PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 11
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGIDisableVBlankVirtualization ENDP
PUBLIC Proxy_DXGIDumpJournal
Proxy_DXGIDumpJournal PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 12
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGIDumpJournal ENDP
PUBLIC Proxy_DXGIGetDebugInterface1
Proxy_DXGIGetDebugInterface1 PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 13
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGIGetDebugInterface1 ENDP
PUBLIC Proxy_DXGIReportAdapterConfiguration
Proxy_DXGIReportAdapterConfiguration PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 14
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_DXGIReportAdapterConfiguration ENDP
PUBLIC Proxy_PIXBeginCapture
Proxy_PIXBeginCapture PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 15
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_PIXBeginCapture ENDP
PUBLIC Proxy_PIXEndCapture
Proxy_PIXEndCapture PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 16
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_PIXEndCapture ENDP
PUBLIC Proxy_PIXGetCaptureState
Proxy_PIXGetCaptureState PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 17
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_PIXGetCaptureState ENDP
PUBLIC Proxy_SetAppCompatStringPointer
Proxy_SetAppCompatStringPointer PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 18
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_SetAppCompatStringPointer ENDP
PUBLIC Proxy_UpdateHMDEmulationStatus
Proxy_UpdateHMDEmulationStatus PROC FRAME
sub rsp, 0C8h
.allocstack 0C8h
.endprolog
mov [rsp+20h], rcx
mov [rsp+28h], rdx
mov [rsp+30h], r8
mov [rsp+38h], r9
movdqu [rsp+40h], xmm0
movdqu [rsp+50h], xmm1
movdqu [rsp+60h], xmm2
movdqu [rsp+70h], xmm3
movdqu [rsp+80h], xmm4
movdqu [rsp+90h], xmm5
mov ecx, 19
call ResolveDxgi
movdqu xmm0, [rsp+40h]
movdqu xmm1, [rsp+50h]
movdqu xmm2, [rsp+60h]
movdqu xmm3, [rsp+70h]
movdqu xmm4, [rsp+80h]
movdqu xmm5, [rsp+90h]
mov rcx, [rsp+20h]
mov rdx, [rsp+28h]
mov r8, [rsp+30h]
mov r9, [rsp+38h]
add rsp, 0C8h
jmp rax
Proxy_UpdateHMDEmulationStatus ENDP
END
