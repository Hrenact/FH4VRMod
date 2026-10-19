@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\pose mkdir build\pose
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\pose_capture.cpp third_party\openvr\lib\win64\openvr_api.lib /Fobuild\pose\pose_capture.obj /Febuild\pose\pose_capture.exe
if errorlevel 1 exit /b 1
copy /Y third_party\openvr\bin\win64\openvr_api.dll build\pose\openvr_api.dll >nul
exit /b %errorlevel%
