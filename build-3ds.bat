@echo off
setlocal

set BUILD_DIR=build-3ds
if "%DEVKITPRO%"=="" set DEVKITPRO=E:\App\devkitPro
if "%DEVKITARM%"=="" set DEVKITARM=%DEVKITPRO%\devkitARM
set "PATH=%DEVKITPRO%\tools\bin;%DEVKITPRO%\msys2\usr\bin;%PATH%"

REM Always start from a clean build directory. The "Unix Makefiles"
REM generator + devkitPro's bundled MSYS2 make has a reproducible bug where
REM adding/removing source files in CMakeLists.txt and reconfiguring in
REM place corrupts a generated compiler_depend.make ("multiple target
REM patterns. Stop.") -- a full reconfigure avoids it, and the 3DS build is
REM fast enough (well under a minute) that this isn't a real cost.
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

echo Configuring for Nintendo 3DS (devkitARM)...
cmake -S . -B "%BUILD_DIR%" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="%DEVKITPRO%\cmake\3DS.cmake" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :error

echo.
echo Building...
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :error

echo.
echo Build succeeded: %BUILD_DIR%\DualityPlayer\DualityPlayer.3dsx
echo Run it via run-3ds.bat to launch it in Citra.
exit /b 0

:error
echo.
echo Build failed. Make sure DEVKITPRO/DEVKITARM point at your devkitPro install
echo (currently: DEVKITPRO=%DEVKITPRO%).
exit /b 1
