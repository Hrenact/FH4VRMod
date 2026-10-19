@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\ui-material-clip-math mkdir build\ui-material-clip-math
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc tools\test_ui_material_clip_math.cpp /Fobuild\ui-material-clip-math\test_ui_material_clip_math.obj /Febuild\ui-material-clip-math\test_ui_material_clip_math.exe
exit /b %errorlevel%
