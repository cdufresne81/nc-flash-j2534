@echo off
rem Puts the Tactrix OpenPort 2.0 on Windows' WinUSB driver for nc-flash-j2534
rem (one administrator prompt). Undo: "Restore Tactrix driver.cmd".
if not exist "%~dp0run_setup.ps1" (
    echo Extract the whole zip first, then run this file from the extracted folder.
    pause
    exit /b 1
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_setup.ps1" -Action Install
echo.
pause
