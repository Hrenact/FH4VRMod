@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\dxgi mkdir build\dxgi
python tools\generate_dxgi_proxy.py
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\dxgi\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /LD src\dxgi_proxy.cpp build\dxgi\dxgi_forwarders.obj /Fobuild\dxgi\dxgi_proxy.obj /Febuild\dxgi\dxgi.dll /link /DEF:src\dxgi_proxy.def
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_dxgi_proxy.cpp /Fobuild\dxgi\test_dxgi_proxy.obj /Febuild\dxgi\test_dxgi_proxy.exe
exit /b %errorlevel%
