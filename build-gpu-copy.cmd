@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\gpu-copy mkdir build\gpu-copy
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\gpu-copy\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\gpu-copy\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_GPU_COPY /DFH4VR_FRAME_CAPTURE /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_HEADTRACK src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\gpu-copy\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\gpu-copy\dxgi.dll /DEF:src\dxgi_proxy.def build\gpu-copy\dxgi_forwarders.obj build\gpu-copy\graphics_probe.obj build\gpu-copy\dxgi_proxy.obj build\gpu-copy\vtable_probe.obj kernel32.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\gpu-copy\test_graphics_probe.obj /Febuild\gpu-copy\test_graphics_probe.exe d3d12.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_frame_capture.cpp /Fobuild\gpu-copy\test_frame_capture.obj /Febuild\gpu-copy\test_frame_capture.exe d3d12.lib windowscodecs.lib ole32.lib
exit /b %errorlevel%


