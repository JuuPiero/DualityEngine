@echo off
REM Locates the devkitPro install directory as a native Windows path and
REM sets DKP_WIN to it. Tried in order:
REM   1. DEVKITPRO, if it's already usable as a Windows path -- not always
REM      true: devkitPro's own bundled MSYS2 (and some installs) export it
REM      as a POSIX path like "/opt/devkitpro", which only resolves inside
REM      that shell's own mount table and is meaningless to cmd.exe/CMake.
REM   2. The registry entry devkitProUpdater writes on install
REM      (HKLM\...\Uninstall\devkitProUpdater, value InstallLocation) --
REM      works regardless of DEVKITPRO's format or the drive/folder chosen.
REM   3. The documented default install path, C:\devkitPro.
REM Callers should treat DKP_WIN as unset (empty) if none of these pan out.
setlocal enabledelayedexpansion
set "DKP_WIN="

if defined DEVKITPRO (
    if exist "%DEVKITPRO%\devkitARM" set "DKP_WIN=%DEVKITPRO%"
)

if not defined DKP_WIN (
    for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\devkitProUpdater" /v InstallLocation 2^>nul ^| findstr InstallLocation') do set "DKP_WIN=%%B"
)

if not defined DKP_WIN (
    for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\devkitProUpdater" /v InstallLocation 2^>nul ^| findstr InstallLocation') do set "DKP_WIN=%%B"
)

if not defined DKP_WIN (
    if exist "C:\devkitPro\devkitARM" set "DKP_WIN=C:\devkitPro"
)

endlocal & set "DKP_WIN=%DKP_WIN%"
