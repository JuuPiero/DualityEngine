@echo off
setlocal

set MINGW_BIN=E:\App\devkitPro\msys2\mingw64\bin
if exist "%MINGW_BIN%\glfw3.dll" set "PATH=%MINGW_BIN%;%PATH%"

if not exist "build\DualityEditor\DualityEditor.exe" (
    echo DualityEditor.exe not found -- run build.bat first.
    exit /b 1
)

"build\DualityEditor\DualityEditor.exe"
