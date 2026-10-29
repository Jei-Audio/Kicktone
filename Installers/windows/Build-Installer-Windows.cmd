@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Kicktone - Windows installer build

rem ==========================================================================
rem  Kicktone Windows installer (v0.5.24; renamed from Tailtone in v0.5.29)
rem
rem    BUILD-INSTALLER-WINDOWS.cmd            clean build + all tests, then the
rem                                           installer (use this for a release)
rem    BUILD-INSTALLER-WINDOWS.cmd full       incremental build + all tests
rem    BUILD-INSTALLER-WINDOWS.cmd skipbuild  package the last build as it is
rem
rem  Needs Inno Setup 6.3 or later (https://jrsoftware.org/isinfo.php).
rem  Output in <package>\Release:
rem    Kicktone-<version>-Windows-Setup.exe   installer (VST3 + standalone)
rem    Kicktone-<version>-Windows-VST3.zip    the plugin for manual install
rem ==========================================================================
rem TT_NO_PAUSE (set by a caller) skips the final pause, as in Build-Windows.cmd.
set "OWN_PAUSE=1"
if defined TT_NO_PAUSE set "OWN_PAUSE="
set "MODE=%~1"
if "%MODE%"=="" set "MODE=clean"
if /I not "%MODE%"=="clean" if /I not "%MODE%"=="full" if /I not "%MODE%"=="skipbuild" (
    echo Unknown option "%~1". Use: BUILD-INSTALLER-WINDOWS.cmd [clean ^| full ^| skipbuild]
    goto failed
)

for %%P in ("%~dp0..\..") do set "PKG=%%~fP"
set "SRC=%PKG%\Source-code"
if not exist "%SRC%\CMakeLists.txt" (
    echo ERROR: "%SRC%\CMakeLists.txt" not found. Keep the Installers folder next to Source-code.
    goto failed
)

set "VERSION="
for /f "tokens=3" %%V in ('findstr /B /C:"project(Tailtone VERSION" "%SRC%\CMakeLists.txt"') do set "VERSION=%%V"
if not defined VERSION (
    echo ERROR: could not read the version from CMakeLists.txt.
    goto failed
)
echo ==========================================================
echo   Kicktone %VERSION% - Windows installer  (%MODE%)
echo ==========================================================

rem Find Inno Setup before spending time on the build.
set "ISCC="
for %%I in (ISCC.exe) do if not "%%~$PATH:I"=="" set "ISCC=%%~$PATH:I"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo ERROR: Inno Setup 6 ^(ISCC.exe^) was not found.
    echo Install it from https://jrsoftware.org/isinfo.php or run:
    echo   winget install --id JRSoftware.InnoSetup -e
    goto failed
)
echo Inno Setup: "%ISCC%"

if /I not "%MODE%"=="skipbuild" (
    echo.
    echo Building Kicktone and running every test ^(%MODE%^)...
    set "TT_NO_PAUSE=1"
    call "%SRC%\Build-Windows.cmd" %MODE%
    if errorlevel 1 (
        echo ERROR: the build or a test failed - no installer was made.
        goto failed
    )
)

if not defined LOCALAPPDATA (
    echo ERROR: LOCALAPPDATA is not set.
    goto failed
)
set "ART=%LOCALAPPDATA%\JeiAudio\Tailtone\Tailtone_artefacts\Release"
set "VST3=%ART%\VST3\Kicktone.vst3"
set "EXE=%ART%\Standalone\Kicktone.exe"
if not exist "%VST3%\Contents\x86_64-win\Kicktone.vst3" (
    echo ERROR: no built plugin at "%VST3%". Run without skipbuild.
    goto failed
)
if not exist "%EXE%" (
    echo ERROR: no built standalone app at "%EXE%". Run without skipbuild.
    goto failed
)

set "OUT=%PKG%\Release"
if not exist "%OUT%" mkdir "%OUT%"
echo.
echo Making the installer...
"%ISCC%" /Qp "/DAppVersion=%VERSION%" "/DVst3Dir=%VST3%" "/DStandaloneExe=%EXE%" "/DPackageDir=%PKG%" "/O%OUT%" "%~dp0Kicktone.iss"
if errorlevel 1 (
    echo ERROR: Inno Setup failed ^(see above^).
    goto failed
)

echo Making the VST3 zip...
set "ZIPDIR=%TEMP%\Kicktone-%VERSION%-zip"
if exist "%ZIPDIR%" rmdir /s /q "%ZIPDIR%"
mkdir "%ZIPDIR%"
robocopy "%VST3%" "%ZIPDIR%\Kicktone.vst3" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto zip_failed
copy /y "%~dp0Kicktone-ReadMe.txt" "%ZIPDIR%\" >nul
copy /y "%SRC%\LICENSE.txt" "%ZIPDIR%\" >nul
copy /y "%SRC%\THIRD-PARTY.md" "%ZIPDIR%\" >nul
if exist "%OUT%\Kicktone-%VERSION%-Windows-VST3.zip" del /q "%OUT%\Kicktone-%VERSION%-Windows-VST3.zip"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%ZIPDIR%\*' -DestinationPath '%OUT%\Kicktone-%VERSION%-Windows-VST3.zip'"
if errorlevel 1 goto zip_failed
rmdir /s /q "%ZIPDIR%"

echo.
echo ==========================================================
echo INSTALLER READY in "%OUT%":
echo   Kicktone-%VERSION%-Windows-Setup.exe
echo   Kicktone-%VERSION%-Windows-VST3.zip
echo Not code-signed: Windows SmartScreen will warn on other PCs.
echo ==========================================================
echo.
if defined OWN_PAUSE pause
exit /b 0

:zip_failed
echo ERROR: could not make the VST3 zip ^(the installer itself is ready^).
goto failed

:failed
echo.
if defined OWN_PAUSE pause
exit /b 1
