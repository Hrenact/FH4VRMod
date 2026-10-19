@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\ui-map-shader-clip mkdir build\ui-map-shader-clip
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc tools\test_ui_map_shader_clip.cpp /Fobuild\ui-map-shader-clip\test_ui_map_shader_clip.obj /Febuild\ui-map-shader-clip\test_ui_map_shader_clip.exe d3d12.lib d3dcompiler.lib dxguid.lib windowscodecs.lib ole32.lib
exit /b %errorlevel%
