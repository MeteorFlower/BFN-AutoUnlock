@echo off
REM ============================================================
REM  AutoUnlock.dll build script (MSVC)
REM  MSVC is required: the host DLL is MSVC-built, and
REM  __try/__except is MSVC-only syntax (GCC does not support it).
REM ============================================================
setlocal
cd /d "%~dp0"

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo [!] vcvars64.bat not found. Edit VCVARS in this script.
    exit /b 1
)

echo [*] Initializing MSVC environment...
call "%VCVARS%" >nul
if errorlevel 1 ( echo [!] vcvars failed & exit /b 1 )

if not exist obj mkdir obj

echo [*] Compiling...
REM /Brepro = reproducible build: no build timestamp goes into the binary, so the
REM same source always produces the same bytes and the hash can be compared.
cl /nologo /LD /O2 /MT /EHsc /W3 /Brepro /D_CRT_SECURE_NO_WARNINGS ^
   AutoUnlock.cpp /Fe:AutoUnlock.dll /Fo:obj\ ^
   /link kernel32.lib
if errorlevel 1 ( echo. & echo [!] BUILD FAILED & exit /b 1 )

echo.
echo [*] Done:
dir /b AutoUnlock.dll
echo.
echo Required files in target folder:
echo    AutoUnlock.dll      - inject this one
echo    items.txt           - unlock ID list, one per line
echo.
echo NOTE: this DLL no longer patches memory. The game folder must already have a
echo       patched RtWorkQ.dll (built from Mods_dev\AutoOffline). Without it the
echo       game cannot reach a map, and granting then fails with no error at all.
