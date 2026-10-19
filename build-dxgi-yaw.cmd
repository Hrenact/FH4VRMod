@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\dxgi-yaw mkdir build\dxgi-yaw
ml64 /nologo /c /Fobuild\dxgi-yaw\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_CAMERA_PROBE /DFH4VR_YAW_TEST src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\dxgi-yaw\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\dxgi-yaw\dxgi.dll /DEF:src\dxgi_proxy.def build\dxgi-yaw\dxgi_forwarders.obj build\dxgi-yaw\dxgi_proxy.obj build\dxgi-yaw\vtable_probe.obj kernel32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_camera_yaw.cpp /Fobuild\test_camera_yaw.obj /Febuild\test_camera_yaw.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_YAW_TEST src\test_vtable_probe.cpp /Fobuild\test_vtable_yaw.obj /Febuild\test_vtable_yaw.exe user32.lib
exit /b %errorlevel%
