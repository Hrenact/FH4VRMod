@echo off
setlocal
call "%~dp0tools\setup-msvc.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\stereo-trial mkdir build\stereo-trial
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_runtime_mode.cpp /Fobuild\stereo-trial\test_runtime_mode.obj /Febuild\stereo-trial\test_runtime_mode.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\query_runtime_mode.cpp /Fobuild\stereo-trial\query_runtime_mode.obj /Febuild\stereo-trial\query_runtime_mode.exe
exit /b %errorlevel%
