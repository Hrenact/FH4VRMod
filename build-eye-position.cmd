@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\eye-position mkdir build\eye-position
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\eye-position\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\eye-position\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /Ithird_party\openvr\headers /DFH4VR_LIVE_OVERLAY /DFH4VR_GPU_COPY /DFH4VR_FRAME_CAPTURE /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\eye-position\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\eye-position\dxgi.dll /DEF:src\dxgi_proxy.def build\eye-position\dxgi_forwarders.obj build\eye-position\graphics_probe.obj build\eye-position\dxgi_proxy.obj build\eye-position\vtable_probe.obj kernel32.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\eye-position\test_graphics_probe.obj /Febuild\eye-position\test_graphics_probe.exe d3d12.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_frame_capture.cpp /Fobuild\eye-position\test_frame_capture.obj /Febuild\eye-position\test_frame_capture.exe d3d12.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_eye_offset.cpp /Fobuild\eye-position\test_eye_offset.obj /Febuild\eye-position\test_eye_offset.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\test_position_probe.cpp /Fobuild\eye-position\test_position_probe.obj /Febuild\eye-position\test_position_probe.exe user32.lib
exit /b %errorlevel%


