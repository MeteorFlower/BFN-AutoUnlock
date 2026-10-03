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
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    if errorlevel 1 (
        echo.
        echo [!] Could not restart with administrator rights.
        echo     Right-click this file and choose "Run as administrator".
        echo.
        pause >nul
    )
    exit /b
)

REM ---- running elevated from here on ----------------------------------------
REM Prefer the packaged exe: players do not need Python installed at all.
REM The .py stays as the source; if there is no exe (dev machine that has not
REM built one yet), fall back to it. Whichever runs is printed, so the log
REM never leaves it ambiguous which path was taken.
REM Call the exe by full path: with NoDefaultCurrentDirectoryInExePath set
REM (some shells set it) a bare "inject_autounlock.exe" is not looked up in the
REM current directory. Full path works everywhere.
if exist "inject_autounlock.exe" (
    echo [*] using inject_autounlock.exe
    "%~dp0inject_autounlock.exe"
) else (
    where python >nul 2>&1
    if errorlevel 1 (
        echo [!] Neither inject_autounlock.exe nor python was found.
        echo.
        echo     The release zip ships the exe. If it is missing, install 64-bit
        echo     Python 3 from https://www.python.org/downloads/windows/
        echo     and tick "Add python.exe to PATH" during setup.
        echo.
        echo ---- press any key to close ----
        pause >nul
        exit /b 1
    )
    echo [*] using python inject_autounlock.py
    python inject_autounlock.py
)
if errorlevel 1 (
    echo.
    echo ---- the injector failed, press any key to close ----
    pause >nul
)
