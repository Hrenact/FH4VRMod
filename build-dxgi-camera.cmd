@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\dxgi-camera mkdir build\dxgi-camera
python tools\generate_dxgi_proxy.py
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\dxgi-camera\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /c /TC /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fobuild\dxgi-camera\
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_CAMERA_PROBE /Ithird_party\minhook\include src\camera_probe.cpp src\dxgi_proxy.cpp /Fobuild\dxgi-camera\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\dxgi-camera\dxgi.dll /DEF:src\dxgi_proxy.def build\dxgi-camera\*.obj kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\minhook\include src\test_camera_hook.cpp build\dxgi-camera\buffer.obj build\dxgi-camera\hook.obj build\dxgi-camera\trampoline.obj build\dxgi-camera\hde64.obj /Fobuild\test_camera_hook.obj /Febuild\test_camera_hook.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_probe_telemetry.cpp /Fobuild\test_probe_telemetry.obj /Febuild\test_probe_telemetry.exe
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\test_camera_prologue_asm.obj src\test_camera_prologue.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DTEST_CAMERA_PROLOGUE /Ithird_party\minhook\include src\test_camera_hook.cpp build\test_camera_prologue_asm.obj build\dxgi-camera\buffer.obj build\dxgi-camera\hook.obj build\dxgi-camera\trampoline.obj build\dxgi-camera\hde64.obj /Fobuild\test_camera_prologue.obj /Febuild\test_camera_prologue.exe
exit /b %errorlevel%
