@echo off
setlocal

set CITRA=E:\App\nightly\citra-qt.exe

if not exist "build-3ds\DualityPlayer\DualityPlayer.3dsx" (
    echo DualityPlayer.3dsx not found -- run build-3ds.bat first.
    exit /b 1
)

if not exist "%CITRA%" (
    echo Citra not found at %CITRA% -- edit run-3ds.bat if it's installed elsewhere.
    exit /b 1
)

start "" "%CITRA%" "%~dp0build-3ds\DualityPlayer\DualityPlayer.3dsx"
