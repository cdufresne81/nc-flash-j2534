@echo off
rem Removes the WinUSB setup (made here, by NC Flash or by Zadig) and puts the
rem Tactrix OpenPort 2.0 back on Tactrix's driver (one administrator prompt).
if not exist "%~dp0run_setup.ps1" (
    echo Extract the whole zip first, then run this file from the extracted folder.
    pause
    exit /b 1
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_setup.ps1" -Action Restore
echo.
pause
