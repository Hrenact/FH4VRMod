@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\aer-deferred mkdir build\aer-deferred
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\aer-deferred\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\aer-deferred\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /Ithird_party\openvr\headers /DFH4VR_AER_DEFERRED /DFH4VR_AER_PROBE /DFH4VR_FRAME_CAPTURE /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\aer-deferred\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\aer-deferred\dxgi.dll /DEF:src\dxgi_proxy.def build\aer-deferred\dxgi_forwarders.obj build\aer-deferred\graphics_probe.obj build\aer-deferred\dxgi_proxy.obj build\aer-deferred\vtable_probe.obj kernel32.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\aer-deferred\test_graphics_probe.obj /Febuild\aer-deferred\test_graphics_probe.exe d3d12.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_frame_capture.cpp /Fobuild\aer-deferred\test_frame_capture.obj /Febuild\aer-deferred\test_frame_capture.exe d3d12.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_eye_offset.cpp /Fobuild\aer-deferred\test_eye_offset.obj /Febuild\aer-deferred\test_eye_offset.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\test_position_probe.cpp /Fobuild\aer-deferred\test_position_probe.obj /Febuild\aer-deferred\test_position_probe.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_AER_DEFERRED src\test_aer_probe.cpp /Fobuild\aer-deferred\test_aer_probe.obj /Febuild\aer-deferred\test_aer_probe.exe
exit /b %errorlevel%


