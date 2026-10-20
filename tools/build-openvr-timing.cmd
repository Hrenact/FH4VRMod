@echo off
setlocal
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\openvr-timing mkdir build\openvr-timing
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers tools\capture_openvr_timing.cpp /Fobuild\openvr-timing\capture_openvr_timing.obj /Febuild\openvr-timing\capture_openvr_timing.exe
exit /b %errorlevel%
