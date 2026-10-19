@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Prepare-VRResolution.ps1" %*
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo Resolution preparation failed. Read the error above before starting FH4.
pause
endlocal & exit /b %result%
