@echo off
setlocal
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\ui-map-clip-math mkdir build\ui-map-clip-math
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_map_clip_math.cpp /Fobuild\ui-map-clip-math\test_ui_map_clip_math.obj /Febuild\ui-map-clip-math\test_ui_map_clip_math.exe
exit /b %errorlevel%
