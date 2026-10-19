@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\aer-probe mkdir build\aer-probe
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\aer-probe\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\aer-probe\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /Ithird_party\openvr\headers /DFH4VR_AER_PROBE /DFH4VR_FRAME_CAPTURE /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\aer-probe\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\aer-probe\dxgi.dll /DEF:src\dxgi_proxy.def build\aer-probe\dxgi_forwarders.obj build\aer-probe\graphics_probe.obj build\aer-probe\dxgi_proxy.obj build\aer-probe\vtable_probe.obj kernel32.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\aer-probe\test_graphics_probe.obj /Febuild\aer-probe\test_graphics_probe.exe d3d12.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_frame_capture.cpp /Fobuild\aer-probe\test_frame_capture.obj /Febuild\aer-probe\test_frame_capture.exe d3d12.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_eye_offset.cpp /Fobuild\aer-probe\test_eye_offset.obj /Febuild\aer-probe\test_eye_offset.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\test_position_probe.cpp /Fobuild\aer-probe\test_position_probe.obj /Febuild\aer-probe\test_position_probe.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_aer_probe.cpp /Fobuild\aer-probe\test_aer_probe.obj /Febuild\aer-probe\test_aer_probe.exe
exit /b %errorlevel%


