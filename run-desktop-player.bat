@echo off
setlocal

call "%~dp0devkitpro-path.bat"
if defined DKP_WIN if exist "%DKP_WIN%\msys2\mingw64\bin\glfw3.dll" set "PATH=%DKP_WIN%\msys2\mingw64\bin;%PATH%"

REM GameScripts.dll lands in build\lib -- not build\GameScripts -- since
REM CMake's GNU/MinGW toolchain places shared-library (.dll) artifacts under
REM a shared lib\ output folder by default (mirroring Unix .so placement),
REM unlike DualityPlayerDesktop.exe itself which lands in its own target
REM folder. Same PATH-based resolution as glfw3.dll/glew32.dll above.
set "PATH=%~dp0build\lib;%PATH%"

if not exist "build\DualityPlayerDesktop\DualityPlayerDesktop.exe" (
    echo DualityPlayerDesktop.exe not found -- run build.bat first.
    exit /b 1
)

"build\DualityPlayerDesktop\DualityPlayerDesktop.exe"
