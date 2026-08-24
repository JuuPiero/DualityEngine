@echo off
setlocal

call "%~dp0devkitpro-path.bat"
if defined DKP_WIN set "PATH=%DKP_WIN%\msys2\mingw64\bin;%PATH%"

if not exist "build\Tests\DualityEngineTests.exe" (
    echo DualityEngineTests.exe not found -- run build.bat first.
    exit /b 1
)

"build\Tests\DualityEngineTests.exe"
exit /b %ERRORLEVEL%
