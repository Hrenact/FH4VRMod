@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_runtime_projection.cpp /Fobuild\stereo-trial\test_runtime_projection.obj /Febuild\stereo-trial\test_runtime_projection.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\test_scene_pose.cpp /Fobuild\stereo-trial\test_scene_pose.obj /Febuild\stereo-trial\test_scene_pose.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_LIVE_SCENE src\test_stereo_head.cpp /Fobuild\stereo-trial\test_scene_head.obj /Febuild\stereo-trial\test_scene_head.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /Ithird_party\openvr\headers src\test_live_scene.cpp /Fobuild\stereo-trial\test_live_scene.obj /Febuild\stereo-trial\test_live_scene.exe d3d12.lib dxgi.lib windowscodecs.lib ole32.lib user32.lib
exit /b %errorlevel%


