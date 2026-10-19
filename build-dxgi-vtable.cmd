@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\dxgi-vtable mkdir build\dxgi-vtable
ml64 /nologo /c /Fobuild\dxgi-vtable\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_CAMERA_PROBE src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\dxgi-vtable\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\dxgi-vtable\dxgi.dll /DEF:src\dxgi_proxy.def build\dxgi-vtable\dxgi_forwarders.obj build\dxgi-vtable\dxgi_proxy.obj build\dxgi-vtable\vtable_probe.obj kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_vtable_probe.cpp /Fobuild\test_vtable_probe.obj /Febuild\test_vtable_probe.exe
exit /b %errorlevel%
