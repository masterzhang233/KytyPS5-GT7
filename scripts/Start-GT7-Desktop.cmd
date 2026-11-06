@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-GT7-Desktop.ps1" %*
set "gt7_exit=%errorlevel%"
if not "%gt7_exit%"=="0" pause
exit /b %gt7_exit%
