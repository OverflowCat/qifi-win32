@echo off
setlocal

REM Build qifi-win32 with MinGW and statically linked zlib.
REM Override the zlib location with:
REM   set ZLIB_ROOT=C:\path\to\mingw64

if "%ZLIB_ROOT%"=="" set "ZLIB_ROOT=%~dp0..\thirdparty\msys64\mingw64"
set "BUILD_DIR=build-mingw-static"

if "%1"=="--clean" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Cleaned %BUILD_DIR%.
    goto :eof
)

echo Configuring MinGW build with static zlib...
cmake -S . -B "%BUILD_DIR%" -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DZLIB_ROOT="%ZLIB_ROOT%"
if errorlevel 1 goto :failed

echo Building...
cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 goto :failed

echo.
echo === Build successful ===
echo Executable: %BUILD_DIR%\qifi-win32.exe
goto :eof

:failed
echo.
echo === Build FAILED ===
exit /b 1
