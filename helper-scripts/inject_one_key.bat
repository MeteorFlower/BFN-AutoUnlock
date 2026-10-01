@echo off
REM ============================================================
REM  Request administrator rights, then run the injector.
REM  (OpenProcess on the game returns err=5 without elevation)
REM ============================================================
setlocal
cd /d "%~dp0."
net session >nul 2>&1
if errorlevel 1 (
    echo [*] Requesting administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~dp0_run_inject.bat' -Verb RunAs"
    exit /b
)
call "%~dp0_run_inject.bat"
