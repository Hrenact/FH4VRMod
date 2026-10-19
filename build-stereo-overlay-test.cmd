@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\overlay mkdir build\overlay
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\test_stereo_overlay.cpp third_party\openvr\lib\win64\openvr_api.lib /Fobuild\overlay\test_stereo_overlay.obj /Febuild\overlay\test_stereo_overlay.exe d3d12.lib dxgi.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
copy /Y third_party\openvr\bin\win64\openvr_api.dll build\overlay\openvr_api.dll >nul
exit /b %errorlevel%
