@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /c /TC /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fobuild\
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /LD /Ithird_party\minhook\include src\camera_probe.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fobuild\camera_probe.obj /Febuild\camera_probe.dll
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\probe_control.cpp /Fobuild\probe_control.obj /Febuild\probe_control.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 /O2 /MT src\probe_host.cpp /Fobuild\probe_host.obj /Febuild\probe_host.exe
exit /b %errorlevel%
