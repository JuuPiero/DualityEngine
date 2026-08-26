@echo off
setlocal

REM Always operate relative to this script's own location, regardless of
REM the caller's current directory (e.g. when invoked from the Editor's
REM "Build for 3DS" button via a plain `system("...build-3ds.bat")` call).
cd /d "%~dp0"

set BUILD_DIR=build-3ds

REM devkitPro's install location, as a native Windows path. NOT taken
REM directly from the DEVKITPRO environment variable: it can legitimately be
REM an MSYS2-style POSIX path (e.g. "/opt/devkitpro", which some shells
REM export it as, and which the MSYS-hosted cmake.exe below is fine with)
REM -- but that same value is useless as a Windows PATH entry (no drive
REM letter), and silently resolving to nothing there previously caused the
REM wrong cmake.exe (a mingw64-flavored one, which can't even parse
REM "/opt/..." paths) to get picked up instead. devkitpro-path.bat resolves
REM the real Windows path (env var if usable, else the registry entry
REM devkitProUpdater writes on install, else the documented default) so
REM this doesn't depend on the install being at any one specific location;
REM the MSYS-notation form is then derived FROM that, rather than trying to
REM detect and reuse whatever format the environment variable happens to be in.
call "%~dp0devkitpro-path.bat"
if not defined DKP_WIN (
    echo Could not locate a devkitPro install ^(checked DEVKITPRO and the registry^).
    echo Install devkitPro from https://devkitpro.org/wiki/Getting_Started, or set
    echo the DEVKITPRO environment variable to your install path.
    exit /b 1
)
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

REM %1, if given, is the active Project's own Assets/Scripts directory (passed by
REM BuildPipeline::BuildFor3DS -- see GameScripts/CMakeLists.txt's
REM DUALITY_PROJECT_SCRIPTS_DIR) so a project's own scripts get compiled into the
REM STATIC 3DS-linked GameScripts too, not just the desktop SHARED DLL. Empty when
REM this script is run directly (double-click/plain invocation, no active project) --
REM matches that CMake cache variable's own empty default, a graceful no-op. Passed
REM through here as a plain Windows-style path, unconverted -- GameScripts/
REM CMakeLists.txt derives its own MSYS-mount-notation copy internally (gated to
REM CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS") for the file(GLOB)/EXISTS calls that
REM need it, the same "F:/..." -> "/f/..." transform this script's own DKP_MSYS
REM derivation above uses, but done in one place instead of duplicated here.
echo Configuring for Nintendo 3DS (devkitARM)...
cmake -S . -B "%BUILD_DIR%" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="%DKP_MSYS%/cmake/3DS.cmake" -DCMAKE_BUILD_TYPE=Release -DDUALITY_PROJECT_SCRIPTS_DIR="%~1"
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
echo Build failed. Detected devkitPro at %DKP_WIN% -- if that's wrong, set the
echo DEVKITPRO environment variable to your actual install path.
exit /b 1
