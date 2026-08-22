@echo off
setlocal

REM Always operate relative to this script's own location, regardless of
REM the caller's current directory (e.g. when invoked from the Editor's
REM "Build for 3DS" button via a plain `system("...build-3ds.bat")` call).
cd /d "%~dp0"

set BUILD_DIR=build-3ds

REM devkitPro's install location. NOT derived from the DEVKITPRO
REM environment variable: it can legitimately be an MSYS2-style POSIX path
REM (e.g. "/opt/devkitpro", which some shells export it as, and which the
REM MSYS-hosted cmake.exe below is fine with) -- but that same value is
REM useless as a Windows PATH entry (no drive letter), and silently
REM resolving to nothing there previously caused the wrong cmake.exe
REM (a mingw64-flavored one, which can't even parse "/opt/..." paths) to
REM get picked up instead. Hardcode the known-good Windows path here and
REM derive the MSYS-notation form FROM it, rather than trying to detect and
REM reuse whatever format the environment variable happens to be in.
set DKP_WIN=E:\App\devkitPro
set "DKP_MSYS=%DKP_WIN:\=/%"
set "DKP_MSYS=%DKP_MSYS::=%"
set "DKP_MSYS=/%DKP_MSYS%"

REM PATH is resolved by cmd.exe/the OS loader, so the plain Windows-style
REM path is what belongs here. Prepended so it wins over any other
REM MSYS2/MinGW install already on PATH (this machine has at least one).
set "PATH=%DKP_WIN%\tools\bin;%DKP_WIN%\msys2\usr\bin;%PATH%"

REM devkitPro's own dkp-initialize-path.cmake requires CMake itself to be
REM the MSYS2-hosted build (E:\App\devkitPro\msys2\usr\bin\cmake.exe, found
REM via the PATH above), and that cmake.exe uses Cygwin/MSYS path semantics
REM internally for CMAKE_TOOLCHAIN_FILE / $ENV{DEVKITPRO} -- MSYS mount
REM notation ("/E/App/devkitPro"), not a plain Windows path.
set "DEVKITPRO=%DKP_MSYS%"
set "DEVKITARM=%DKP_MSYS%/devkitARM"

REM Always start from a clean build directory. The "Unix Makefiles"
REM generator + devkitPro's bundled MSYS2 make has a reproducible bug where
REM adding/removing source files in CMakeLists.txt and reconfiguring in
REM place corrupts a generated compiler_depend.make ("multiple target
REM patterns. Stop.") -- a full reconfigure avoids it, and the 3DS build is
REM fast enough (well under a minute) that this isn't a real cost. Retries
REM a couple of times since Windows occasionally holds a brief transient
REM lock on a just-written file (antivirus/indexer) right after a build.
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%" 2>nul
    if exist "%BUILD_DIR%" (
        timeout /t 1 /nobreak >nul
        rmdir /s /q "%BUILD_DIR%" 2>nul
    )
    if exist "%BUILD_DIR%" (
        echo Could not remove "%BUILD_DIR%" -- a file inside it is still locked by another process.
        exit /b 1
    )
)

echo Configuring for Nintendo 3DS (devkitARM)...
cmake -S . -B "%BUILD_DIR%" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="%DKP_MSYS%/cmake/3DS.cmake" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :error

echo.
echo Building...
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :error

echo.
echo Build succeeded:
echo   %BUILD_DIR%\DualityPlayer\DualityPlayer.3dsx  (Homebrew Launcher / 3dslink)
echo   %BUILD_DIR%\DualityPlayer\DualityPlayer.cia    (installable via FBI on real hardware/CFW)
echo Run the .3dsx via run-3ds.bat to launch it in Citra.
exit /b 0

:error
echo.
echo Build failed. Make sure devkitPro is installed at %DKP_WIN%
echo (edit build-3ds.bat's DKP_WIN if yours is elsewhere).
exit /b 1
