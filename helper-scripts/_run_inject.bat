@echo off
cd /d "%~dp0."
python inject_autounlock.py
if errorlevel 1 (
    echo.
    echo ---- the injector failed, press any key to close ----
    pause >nul
)
