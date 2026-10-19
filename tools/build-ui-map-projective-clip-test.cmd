@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\ui-map-projective-clip mkdir build\ui-map-projective-clip
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc tools\test_ui_map_projective_clip.cpp /Fobuild\ui-map-projective-clip\test_ui_map_projective_clip.obj /Febuild\ui-map-projective-clip\test_ui_map_projective_clip.exe d3d12.lib d3dcompiler.lib dxguid.lib windowscodecs.lib ole32.lib
exit /b %errorlevel%
