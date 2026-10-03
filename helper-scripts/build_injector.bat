@echo off
REM ============================================================
REM  Build inject_autounlock.exe (PyInstaller, onefile, console).
REM
REM  Why: the injector used to need Python installed on the player's
REM  machine. This packs the interpreter into the exe, so players
REM  only need the exe itself. _run_inject.bat prefers the exe and
REM  falls back to the .py on a machine that has Python.
REM
REM  Dev machine only: needs python + pyinstaller.
REM  Output: inject_autounlock.exe next to this script.
REM  Build work files go to %TEMP%\autounlock_pyi so the repo stays
REM  clean (no build/ dist/ .spec clutter).
REM
REM  Deliberately NOT using --uac-admin: elevation stays in
REM  inject_main.bat. If it were baked into the exe, launching it by
REM  hand would pop a UAC prompt out of nowhere; the way it is now,
REM  running it unelevated just prints a clear "use inject_main.bat"
REM  message instead.
REM
REM  Reproducible build: two environment variables make the output
REM  byte-identical across runs (verified 2026-10-03 by building twice and
REM  comparing hashes). SOURCE_DATE_EPOCH fixes the PE header timestamp;
REM  PYTHONHASHSEED fixes the hash-randomized iteration order inside
REM  PyInstaller, which the archive layout depends on. With only the first
REM  one set, two builds still differ.
REM  Caveat: reproducible on the same toolchain (same Python + PyInstaller
REM  versions, same paths). A different PyInstaller version builds different
REM  bytes even from the same source, so a hash is only worth comparing if
REM  the versions match too.
REM  The epoch value itself is arbitrary - it only ends up in the PE header,
REM  nothing reads it. 1 is used on purpose: it is obviously a fixed marker
REM  rather than a real build time, and it avoids 0, which tooling tends to
REM  treat as "unset". What matters is that it never changes: change it and
REM  every future build hashes differently from every published one.
set SOURCE_DATE_EPOCH=1
set PYTHONHASHSEED=0
REM ============================================================
setlocal
cd /d "%~dp0"

where python >nul 2>&1
if errorlevel 1 (
    echo [!] python was not found on PATH. This build script needs python.
    exit /b 1
)

echo [*] Checking pyinstaller...
python -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo [!] PyInstaller is missing. Install it with:
    echo     python -m pip install pyinstaller
    exit /b 1
)

echo [*] Building inject_autounlock.exe ...
python -m PyInstaller --noconfirm --clean --onefile --console --noupx ^
   --name inject_autounlock ^
   --distpath . --workpath "%TEMP%\autounlock_pyi" --specpath "%TEMP%\autounlock_pyi" ^
   inject_autounlock.py
if errorlevel 1 (
    echo.
    echo [!] BUILD FAILED - see the PyInstaller output above.
    exit /b 1
)

echo.
echo [*] Done: inject_autounlock.exe
echo     Players run inject_main.bat as before; it picks the exe automatically.
echo     The .py stays as the source, keep it in this folder.
echo     Note: .gitignore excludes *.exe, so this file is not committed on
echo     purpose - include it in the release zip instead.
