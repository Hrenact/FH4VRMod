@echo off
rem Keep the selected toolchain environment in the calling build script.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0
set "FH4VR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FH4VR_VSWHERE%" (
    echo Visual Studio 2022 C++ x64 build tools and Windows SDK are required.
    exit /b 1
)
set "FH4VR_VS_ROOT="
for /f "usebackq tokens=*" %%i in (`"%FH4VR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FH4VR_VS_ROOT=%%i"
if not defined FH4VR_VS_ROOT (
    echo No Visual Studio installation with C++ x64 build tools was found.
    exit /b 1
)
call "%FH4VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
exit /b %errorlevel%
