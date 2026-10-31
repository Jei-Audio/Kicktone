@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Kicktone - Visual Studio 2026 VST3 Build

rem ==========================================================================
rem  Kicktone Windows build (v0.5.6 onward: fast incremental builds)
rem
rem    BUILD-WINDOWS.cmd            quick: only what changed, VST3 + standalone,
rem                                 no tests, then installs the VST3 (default)
rem    BUILD-WINDOWS.cmd full       as quick, plus all tests built and run
rem    BUILD-WINDOWS.cmd clean      wipe the build folder first, then full
rem
rem  The package (Source-code + Samples) is mirrored to a fixed folder
rem  (%LOCALAPPDATA%\JeiAudio\Tailtone-src) so a new version extracted anywhere only recompiles the
rem  files that changed. The build folder is never wiped unless you say clean.
rem  The mirror keeps its own .git folder (public git repository, v0.5.28).
rem ==========================================================================
set "MODE=%~1"
if /I "%MODE%"=="" set "MODE=quick"
if /I "%MODE%"=="-quick" set "MODE=quick"
if /I "%MODE%"=="-full" set "MODE=full"
if /I "%MODE%"=="-clean" set "MODE=clean"
if /I not "%MODE%"=="quick" if /I not "%MODE%"=="full" if /I not "%MODE%"=="clean" (
    echo Unknown option "%~1". Use: BUILD-WINDOWS.cmd [quick ^| full ^| clean]
    goto failed
)

echo ==========================================================
echo   Kicktone - Windows VST3 build for VS 2026  (%MODE%)
echo ==========================================================
echo.

where cmake.exe >nul 2>&1
if errorlevel 1 goto cmake_missing

set "CMAKE_VERSION="
for /f "tokens=3" %%V in ('cmake --version ^| findstr /B /C:"cmake version"') do set "CMAKE_VERSION=%%V"
if not defined CMAKE_VERSION goto cmake_missing
set "CMAKE_MAJOR=0"
set "CMAKE_MINOR=0"
for /f "tokens=1,2 delims=." %%A in ("%CMAKE_VERSION%") do (
    set /a CMAKE_MAJOR=%%A
    set /a CMAKE_MINOR=%%B
)
echo Detected CMake %CMAKE_VERSION%
if %CMAKE_MAJOR% LSS 4 goto cmake_too_old
if %CMAKE_MAJOR% EQU 4 if %CMAKE_MINOR% LSS 2 goto cmake_too_old

cmake --help | findstr /C:"Visual Studio 18 2026" >nul 2>&1
if errorlevel 1 goto generator_missing

if not defined LOCALAPPDATA goto localappdata_missing
set "SRC_DIR=%LOCALAPPDATA%\JeiAudio\Tailtone-src"
set "CMAKE_SRC=%SRC_DIR%\Source-code"
set "BUILD_DIR=%LOCALAPPDATA%\JeiAudio\Tailtone"
set "JUCE_TOOLS_DIR=%BUILD_DIR%"
set "CMAKE_GENERATOR=Visual Studio 18 2026"
set "CMAKE_GENERATOR_PLATFORM=x64"
echo Kicktone ^(formerly Tailtone; permanent plugin ID TaA2; the version is in CMakeLists.txt^)

if /I "%MODE%"=="clean" (
    echo Cleaning generated build: "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" goto clean_failed
)

echo.
echo [1/5] Mirroring the package to "%SRC_DIR%" ^(only changed files are copied^)...
if not exist "%SRC_DIR%" mkdir "%SRC_DIR%"
robocopy "%~dp0.." "%SRC_DIR%" /MIR /NFL /NDL /NJH /NJS /NP /R:2 /W:1 /XD History .git "%~dp0..\Release" "%BUILD_DIR%"
if errorlevel 8 goto mirror_failed
if not exist "%CMAKE_SRC%\ThirdParty\JUCE\extras\Build\CMake\JUCEModuleSupport.cmake" (
    echo NOTICE: Bundled JUCE support file is missing in the extracted folder.
    echo CMake will try to download the matching JUCE version from GitHub.
) else (
    echo Bundled JUCE CMake support: PRESENT
)

echo.
rem A build folder made from another source location (versions before v0.5.6
rem built straight from the extracted folder) cannot be reused: start fresh.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    set "CACHE_SRC="
    for /f "tokens=1,* delims==" %%A in ('findstr /B /C:"CMAKE_HOME_DIRECTORY:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHE_SRC=%%B"
    set "WANT_SRC=%CMAKE_SRC:\=/%"
    call :compare_source
)
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [2/5] Build folder found: configuring only if CMakeLists.txt changed...
    cmake -S "%CMAKE_SRC%" -B "%BUILD_DIR%"
) else (
    echo [2/5] Configuring for Visual Studio 2026 x64 ^(first build: this takes a while^)...
    cmake --fresh -S "%CMAKE_SRC%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" -A x64
)
if errorlevel 1 goto config_failed

rem v0.5.11: recompile by CONTENT. A changed file may carry an older date than the
rem objects of an earlier build (a replaced package keeps its own file dates); Visual
rem Studio would then keep stale objects and the plugin crashes in FL Studio. The
rem script gives every file whose SHA-256 changed since the last successful build
rem the current date. Unchanged files keep their dates, so quick builds stay quick.
echo.
echo [2b/5] Checking which source files changed ^(by content, not by date^)...
set "HASH_STATE=%BUILD_DIR%\tailtone-source.sha256"
if exist "%HASH_STATE%.pending" del /q "%HASH_STATE%.pending"
powershell -NoProfile -ExecutionPolicy Bypass -File "%CMAKE_SRC%\scripts\sync-timestamps.ps1" -Src "%CMAKE_SRC%" -State "%HASH_STATE%"
if errorlevel 1 echo WARNING: could not compare the source files. If Kicktone crashes after this build, run: BUILD-WINDOWS.cmd clean

echo.
if /I "%MODE%"=="quick" (
    echo [3/5] Compiling Kicktone VST3 and standalone ^(only changed files^)...
    cmake --build "%BUILD_DIR%" --config Release --target Tailtone_VST3 Tailtone_Standalone --parallel
) else (
    echo [3/5] Compiling Kicktone VST3, standalone and all tests ^(only changed files^)...
    cmake --build "%BUILD_DIR%" --config Release --target Tailtone_VST3 Tailtone_Standalone TailtoneCpuOptimizationTests TailtoneDecayNoteTests TailtoneIdentityTests TailtoneBassMonoTests TailtoneEqPositionTests TailtoneSplitNormalizeTests TailtoneExtremeV5Tests TailtoneSpectralJoinTests TailtoneParallelRenderTests TailtonePreparationGateTests TailtoneSplitPairTests TailtoneTailStartTests TailtonePlayedKeysTests TailtonePreparationProgressTests TailtoneAttackLayerTests TailtoneOutputLimiterTests TailtonePitchDropTests TailtonePitchDropIntegrationTests TailtoneWetAlignTests TailtoneTailEndCutTests TailtoneRootRenderPhaseTests TailtoneFormantDuckTests TailtoneGlideLayerTests TailtoneKeyMappingTests TailtoneKickCharacterTests TailtoneKickCharacterIntegrationTests TailtoneManualRootTests TailtoneMasterEnvelopeTests TailtoneMasterEnvelopeIntegrationTests TailtonePunchRegionTests TailtonePunchRegionIntegrationTests TailtoneSubDuckTests TailtoneReverbTests TailtoneReverbPitchTests TailtoneReverbIntegrationTests TailtoneAdaptiveStretchTests TailtoneLinearPhaseTests TailtoneTailWetMonoTests TailtoneLinearIntegrationTests TailtoneJoinTrimTests TailtoneSilenceStateTests TailtonePunchPitchTests TailtoneProcessedPreviewTests TailtonePhaseAlignmentTests TailtonePunchTransientTests TailtoneParametricEQTests TailtoneThreeBusEQTests PunchTailCoreTests PunchTailWaveformTests PunchTailKickOverviewTests PunchTailRealTailTests PunchTailWindowsMacroTests PunchTailBankFallbackTests PunchTailStretchTests PunchTailInterpolationTests PunchTailPlaybackTests PunchTailWaveformEditingTests PunchTailEditorTests PunchTailPitchContinuityTests PunchTailReverbLayerTests PunchTailReverbLowCutTests PunchTailDistortionRoutingTests PunchTailDistortionDSPTests --parallel
)
if errorlevel 1 goto build_failed
rem Remember what this successful build was compiled from ^(see step 2b^).
if exist "%HASH_STATE%.pending" move /y "%HASH_STATE%.pending" "%HASH_STATE%" >nul

echo.
if /I "%MODE%"=="quick" (
    echo [4/5] Tests skipped ^(run "BUILD-WINDOWS.cmd full" to build and run them^).
) else (
    echo [4/5] Running tests...
    ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
    if errorlevel 1 goto tests_failed
)

echo.
echo [5/5] Installing the plugin...
set "PLUGIN="
for /d %%G in ("%BUILD_DIR%\Tailtone_artefacts\Release\VST3\Kicktone*.vst3") do set "PLUGIN=%%~fG"
if not defined PLUGIN goto plugin_missing
if not exist "%PLUGIN%" goto plugin_missing
set "INSTALLED=0"
if defined CommonProgramFiles (
    robocopy "%PLUGIN%" "%CommonProgramFiles%\VST3\Kicktone.vst3" /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 >nul 2>&1
    rem v0.5.24: robocopy exits 0 when it cannot open the destination folder at all
    rem ^(no administrator rights^), so compare the installed plugin with the new one.
    fc /b "%PLUGIN%\Contents\x86_64-win\Kicktone.vst3" "%CommonProgramFiles%\VST3\Kicktone.vst3\Contents\x86_64-win\Kicktone.vst3" >nul 2>&1
    if not errorlevel 1 set "INSTALLED=1"
    rem v0.5.29: the plugin was renamed. The old Tailtone.vst3 has the same plugin ID,
    rem so remove it once Kicktone.vst3 is in place ^(the DAW would list it twice^).
    if exist "%CommonProgramFiles%\VST3\Kicktone.vst3\Contents\x86_64-win\Kicktone.vst3" if exist "%CommonProgramFiles%\VST3\Tailtone.vst3" rmdir /s /q "%CommonProgramFiles%\VST3\Tailtone.vst3"
)

echo.
echo ==========================================================
echo BUILD SUCCEEDED
echo VST3 location:
echo   "%PLUGIN%"
if "%INSTALLED%"=="1" (
    echo Installed to "%CommonProgramFiles%\VST3\Kicktone.vst3".
    echo FL Studio picks it up on the next plugin scan ^(or reopen the project^).
) else (
    echo Could not copy it to "%CommonProgramFiles%\VST3\Kicktone.vst3" automatically
    echo ^(no permission, or FL Studio has the plugin open^). Close FL Studio and run this
    echo script as administrator, or copy the Kicktone.vst3 folder there yourself.
    echo Or run BUILD-INSTALLER-WINDOWS.cmd skipbuild and install that ^(it asks for permission^).
    echo Until then your DAW still loads the OLD version.
)
if exist "%CommonProgramFiles%\VST3\Tailtone.vst3" if exist "%CommonProgramFiles%\VST3\Kicktone.vst3" echo NOTE: the old "Tailtone.vst3" is still installed - remove it ^(same plugin ID as Kicktone^).
if exist "%CommonProgramFiles%\VST3\Tailtone v0.4.69 Experimental.vst3" echo NOTE: "Tailtone v0.4.69 Experimental.vst3" is still installed - remove it ^(same plugin ID^).
echo ==========================================================
echo.
if not defined TT_NO_PAUSE pause
exit /b 0

:compare_source
if /I "%CACHE_SRC%"=="%WANT_SRC%" exit /b 0
echo The build folder was made for another source location:
echo   %CACHE_SRC%
echo Starting fresh once ^(this is the full build; later ones are quick^).
rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" goto clean_failed
exit /b 0

:cmake_missing
echo ERROR: cmake.exe is not on PATH.
echo Install CMake 4.2 or later and select Add CMake to PATH.
goto failed

:cmake_too_old
echo ERROR: CMake %CMAKE_VERSION% is too old for Visual Studio 2026.
echo Install CMake 4.2 or later from https://cmake.org/download/
echo Close and reopen Command Prompt after installation.
goto failed

:generator_missing
echo ERROR: The installed CMake does not list Visual Studio 18 2026.
echo Update CMake to 4.2 or later and check CMake PATH priority.
goto failed

:mirror_failed
echo ERROR: Could not mirror the sources to "%SRC_DIR%" ^(robocopy failed^).
goto failed

:config_failed
echo.
echo ERROR: Configure failed. Read the first CMake error above.
echo Check that VS 2026 has the Desktop development with C++ workload,
echo MSVC v145 x64 tools, CMake tools, and the Windows 10/11 SDK installed.
echo If the error mentions a changed generator or source folder, run
echo   BUILD-WINDOWS.cmd clean
goto failed

:localappdata_missing
echo ERROR: LOCALAPPDATA is not set.
echo Open a new Command Prompt and check the Windows LOCALAPPDATA setting.
goto failed

:clean_failed
echo ERROR: Could not clear the build directory:
echo   "%BUILD_DIR%"
echo Close Visual Studio and any processes using this build, then retry.
goto failed

:build_failed
echo ERROR: Compilation failed; copy the first compiler error above.
echo If it looks like a stale build, run: BUILD-WINDOWS.cmd clean
goto failed

:tests_failed
echo ERROR: Compilation succeeded, but one or more tests failed.
goto failed

:plugin_missing
echo ERROR: Build completed but plugin was not found at:
echo   "%PLUGIN%"
echo Look for Kicktone.vst3 under "%BUILD_DIR%".
goto failed

:failed
echo.
echo Send the first detailed error message ^(above^) for diagnosis.
if not defined TT_NO_PAUSE pause
exit /b 1
