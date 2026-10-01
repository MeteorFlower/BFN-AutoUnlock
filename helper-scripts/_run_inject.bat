@echo off
cd /d "%~dp0."

where python >nul 2>&1
if errorlevel 1 (
    echo [!] Python was not found on PATH.
    echo.
    echo     The injector is a Python script. Install 64-bit Python 3 from
    echo       https://www.python.org/downloads/windows/
    echo     and tick "Add python.exe to PATH" during setup.
    echo.
    echo ---- press any key to close ----
    pause >nul
    exit /b 1
)

python inject_autounlock.py
if errorlevel 1 (
    echo.
    echo ---- the injector failed, press any key to close ----
    pause >nul
)
