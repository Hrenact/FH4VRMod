@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\wrapper mkdir build\wrapper
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\wrapper\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\wrapper\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_HEADTRACK src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\wrapper\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\wrapper\dxgi.dll /DEF:src\dxgi_proxy.def build\wrapper\dxgi_forwarders.obj build\wrapper\graphics_probe.obj build\wrapper\dxgi_proxy.obj build\wrapper\vtable_probe.obj kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\wrapper\test_graphics_probe.obj /Febuild\wrapper\test_graphics_probe.exe d3d12.lib user32.lib
exit /b %errorlevel%

