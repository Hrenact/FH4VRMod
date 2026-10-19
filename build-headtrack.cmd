@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\headtrack mkdir build\headtrack
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\pose_bridge.cpp third_party\openvr\lib\win64\openvr_api.lib user32.lib /Fobuild\headtrack\pose_bridge.obj /Febuild\headtrack\pose_bridge.exe
if errorlevel 1 exit /b 1
copy /Y third_party\openvr\bin\win64\openvr_api.dll build\headtrack\openvr_api.dll >nul
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\headtrack\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /DFH4VR_CAMERA_PROBE /DFH4VR_HEADTRACK src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\headtrack\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\headtrack\dxgi.dll /DEF:src\dxgi_proxy.def build\headtrack\dxgi_forwarders.obj build\headtrack\dxgi_proxy.obj build\headtrack\vtable_probe.obj kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_head_pose.cpp /Fobuild\headtrack\test_head_pose.obj /Febuild\headtrack\test_head_pose.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_headtrack_callback.cpp /Fobuild\headtrack\test_headtrack_callback.obj /Febuild\headtrack\test_headtrack_callback.exe
exit /b %errorlevel%
