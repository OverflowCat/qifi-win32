@echo off
REM build_msvc.bat — Build with MSVC (Visual Studio Developer Command Prompt)
REM
REM Prerequisites:
REM   - Open "x64 Native Tools Command Prompt for VS 2022" (or similar)
REM   - zlib: set ZLIB_DIR to zlib install, or use vcpkg
REM
REM Usage:
REM   build_msvc.bat
REM   build_msvc.bat --clean

if "%1"=="--clean" (
    del /q *.obj *.res qifi-win32.exe 2>nul
    echo Cleaned.
    goto :eof
)

set ZLIB_DIR=C:\vcpkg\installed\x64-windows
if not "%ZLIB_DIR_EXTRA%"=="" set ZLIB_DIR=%ZLIB_DIR_EXTRA%

echo Compiling...
cl /nologo /O2 /c /Ilib /Isrc ^
    /I"%ZLIB_DIR%\include" ^
    src\main.c src\qr_window.c lib\qrcodegen.c

echo Resources...
rc /nologo resource.rc

echo Linking...
cl /nologo /Fe:qifi-win32.exe ^
    main.obj qr_window.obj qrcodegen.obj resource.res ^
    /link /SUBSYSTEM:WINDOWS ^
    user32.lib gdi32.lib comdlg32.lib comctl32.lib ^
    "%ZLIB_DIR%\lib\zlib.lib"

if %errorlevel%==0 (
    echo.
    echo === Build successful: qifi-win32.exe ===
) else (
    echo.
    echo === Build FAILED ===
)
