@echo off
setlocal

set BUILD_DIR=build

REM Always prefer the devkitPro-bundled mingw64 toolchain (verified to work
REM with this project's GLFW/GLEW/CMake) over whatever else might already be
REM on PATH -- a machine can easily have another, unrelated MSYS2/ninja
REM install that gets picked up first otherwise and mismatches with it,
REM breaking CMake's compiler sanity check ("not able to compile a simple
REM test program"). devkitpro-path.bat locates the install itself (env var
REM or registry) instead of hardcoding a path specific to one machine.
set "MINGW_BIN="
call "%~dp0devkitpro-path.bat"
if defined DKP_WIN set "MINGW_BIN=%DKP_WIN%\msys2\mingw64\bin"

set "GOT_MINGW="
if defined MINGW_BIN if exist "%MINGW_BIN%\g++.exe" set "GOT_MINGW=1"

if defined GOT_MINGW (
    set "PATH=%MINGW_BIN%;%PATH%"
) else (
    where g++ >nul 2>nul
    if not %ERRORLEVEL%==0 (
        echo Could not find a C++ compiler on PATH, and no mingw64 toolchain found under devkitPro.
        echo Install it from an MSYS2 shell with:
        echo   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw mingw-w64-x86_64-glew mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
        exit /b 1
    )
)

if exist "%MINGW_BIN%\ninja.exe" (
    set GENERATOR=Ninja
) else (
    where ninja >nul 2>nul
    if %ERRORLEVEL%==0 (
        set GENERATOR=Ninja
    ) else (
        set GENERATOR=MinGW Makefiles
    )
)

echo Using compiler: & where g++
echo Using generator: %GENERATOR%

echo Configuring (%GENERATOR%)...
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ >nul 2>nul
if errorlevel 1 (
    echo Build cache in "%BUILD_DIR%" looks stale or incompatible, recreating it...
    rmdir /s /q "%BUILD_DIR%" 2>nul
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
    if errorlevel 1 goto :error
)

echo.
echo Building...
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :error

echo.
echo Build succeeded: %BUILD_DIR%\DualityEditor\DualityEditor.exe
echo Run it via run.bat (sets PATH so glfw3.dll/glew32.dll resolve).
exit /b 0

:error
echo.
echo Build failed. Make sure CMake and a MinGW-w64 toolchain (gcc/g++, plus Ninja or mingw32-make) are on PATH.
exit /b 1
