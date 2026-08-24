@echo off
setlocal

call "%~dp0devkitpro-path.bat"
if defined DKP_WIN if exist "%DKP_WIN%\msys2\mingw64\bin\glfw3.dll" set "PATH=%DKP_WIN%\msys2\mingw64\bin;%PATH%"

if not exist "build\DualityEditor\DualityEditor.exe" (
    echo DualityEditor.exe not found -- run build.bat first.
    exit /b 1
)

"build\DualityEditor\DualityEditor.exe"
