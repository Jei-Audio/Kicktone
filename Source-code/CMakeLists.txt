cmake_minimum_required(VERSION 3.22)
project(Tailtone VERSION 0.5.28 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# v0.5.24: link the MSVC C++ runtime into every binary (/MT, /MTd for Debug), so
# Tailtone loads on PCs without a (new enough) Visual C++ Redistributable. Set
# before any target: the tests link the plugin code and must use the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Prevent Windows.h min/max macros in every plugin and regression-test target.
# Header tests also deliberately inject macros to exercise protected calls.
add_compile_definitions(NOMINMAX)
option(PT_CORE_ONLY "Build only portable DSP tests" OFF)

# r8brain-free-src, Aleksey Vaneev (MIT): header-only, high-quality SRC.
# This is not by itself a time-preserving pitch shifter; Source/PitchShift.h
# adds phase-synchronous waveform-cycle reconstruction.
set(PT_BUNDLED_R8BRAIN "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/r8brain")
if(EXISTS "${PT_BUNDLED_R8BRAIN}/CDSPResampler.h")
    set(r8brain_SOURCE_DIR "${PT_BUNDLED_R8BRAIN}")
else()
    message(FATAL_ERROR "Bundled r8brain is missing. Fully extract the Tailtone ZIP again.")
endif()
find_package(Threads REQUIRED)   # v0.4.65 parallel note rendering
add_library(pt_r8brain INTERFACE)
target_include_directories(pt_r8brain INTERFACE "${r8brain_SOURCE_DIR}")
# v0.4.65: every engine user may render notes on std::thread (ParallelRender.h).
target_link_libraries(pt_r8brain INTERFACE Threads::Threads)

enable_testing()
# v0.5.28 public source: the WAV fixtures in ../Samples are not GPL and are not
# published. A test whose samples are missing is registered as SKIPPED (never
# removed, never reported as passed). With the samples present every test runs
# exactly as before. Re-run CMake after adding the samples.
set(TAILTONE_SAMPLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Samples" CACHE PATH "Folder with the WAV test fixtures")
function(pt_sample_test name target)
    set(paths)
    set(missing)
    foreach(sample IN LISTS ARGN)
        set(path "${TAILTONE_SAMPLES_DIR}/${sample}")
        list(APPEND paths "${path}")
        if(NOT EXISTS "${path}")
            list(APPEND missing "${sample}")
        endif()
    endforeach()
    if(missing)
        list(JOIN missing ", " missingText)
        add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}" -E echo "SKIPPED: test sample missing: ${missingText}")
        set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED: test sample missing")
    else()
        add_test(NAME ${name} COMMAND ${target} ${paths})
    endif()
endfunction()
# v0.4.61: HighFidelitySpectralBalance, BidirectionalCrunchPreservation,
# HighFidelityPitchEngine and ExtremeTailPreservation were removed together with
# the Legacy v1 / High Fidelity / Extreme engines they tested (user request).
add_executable(TailtonePhaseAlignmentTests Tests/PhaseAlignmentTests.cpp)
add_test(NAME PerNotePhaseAlignment COMMAND TailtonePhaseAlignmentTests)
add_executable(PunchTailCoreTests Tests/CoreTests.cpp)
target_link_libraries(PunchTailCoreTests PRIVATE pt_r8brain)
add_test(NAME CoreDSP COMMAND PunchTailCoreTests)
add_executable(PunchTailWaveformTests Tests/WaveformTests.cpp)
add_test(NAME WaveformEnvelope COMMAND PunchTailWaveformTests)
add_executable(PunchTailKickOverviewTests Tests/KickOverviewTests.cpp)
add_test(NAME CompleteKickWaveform COMMAND PunchTailKickOverviewTests)
# This regression uses assert(), including in the Windows Release test gate.
if(MSVC)
    target_compile_options(PunchTailKickOverviewTests PRIVATE /UNDEBUG)
else()
    target_compile_options(PunchTailKickOverviewTests PRIVATE -UNDEBUG)
endif()
# Compile regression: legacy Windows min/max macros must not break DSP headers.
add_executable(PunchTailWindowsMacroTests Tests/WindowsMacroTests.cpp)
target_link_libraries(PunchTailWindowsMacroTests PRIVATE pt_r8brain)
add_test(NAME WindowsMacroCompatibility COMMAND PunchTailWindowsMacroTests)
add_executable(PunchTailRealTailTests Tests/RealTailTests.cpp)
target_link_libraries(PunchTailRealTailTests PRIVATE pt_r8brain)
pt_sample_test(RealTailFixedLength PunchTailRealTailTests tail.wav)
add_executable(PunchTailBankFallbackTests Tests/BankFallbackTests.cpp)
add_test(NAME AudibleTailFallback COMMAND PunchTailBankFallbackTests)
add_executable(PunchTailStretchTests Tests/StretchTests.cpp)
target_link_libraries(PunchTailStretchTests PRIVATE pt_r8brain)
add_test(NAME StretchPitchAndDuration COMMAND PunchTailStretchTests)
add_executable(PunchTailInterpolationTests Tests/InterpolationTests.cpp)
add_test(NAME InterpolationAccuracy COMMAND PunchTailInterpolationTests)
add_executable(PunchTailWaveformEditingTests Tests/WaveformEditingTests.cpp)
add_test(NAME WaveformEditingAndDetection COMMAND PunchTailWaveformEditingTests)
add_executable(PunchTailPitchContinuityTests Tests/PitchContinuityTests.cpp)
target_link_libraries(PunchTailPitchContinuityTests PRIVATE pt_r8brain)
pt_sample_test(DifficultKickPitchContinuity PunchTailPitchContinuityTests virus-kick-3.wav)
add_executable(PunchTailReverbLayerTests Tests/ReverbLayerTests.cpp)
add_test(NAME IndependentReverbAtPunchEnd COMMAND PunchTailReverbLayerTests)
add_executable(PunchTailReverbLowCutTests Tests/ReverbLowCutTests.cpp)
add_test(NAME ReverbLowCut200Hz COMMAND PunchTailReverbLowCutTests)
add_executable(PunchTailDistortionRoutingTests Tests/DistortionRoutingTests.cpp)
add_test(NAME DistortionRoutingAndMidiDefaults COMMAND PunchTailDistortionRoutingTests)
add_executable(TailtoneParametricEQTests Tests/ParametricEQTests.cpp)
add_test(NAME TailtoneParametricEQ COMMAND TailtoneParametricEQTests)
add_executable(TailtoneThreeBusEQTests Tests/ThreeBusEQTests.cpp)
add_test(NAME ThreeIndependentEQBuses COMMAND TailtoneThreeBusEQTests)
add_executable(TailtonePunchTransientTests Tests/PunchTransientTests.cpp)
add_test(NAME PunchTransientDSP COMMAND TailtonePunchTransientTests)
add_executable(TailtonePunchPitchTests Tests/PunchPitchTests.cpp)
target_link_libraries(TailtonePunchPitchTests PRIVATE pt_r8brain)
pt_sample_test(IndependentPunchPitch TailtonePunchPitchTests punch.wav)
# v0.5.5 punch decay note (portable).
add_executable(TailtoneDecayNoteTests Tests/DecayNoteTests.cpp)
target_link_libraries(TailtoneDecayNoteTests PRIVATE pt_r8brain)
add_test(NAME PunchDecayNote COMMAND TailtoneDecayNoteTests)
add_executable(TailtoneJoinTrimTests Tests/JoinTrimTests.cpp)
add_test(NAME TransitionGainAndSilenceTrim COMMAND TailtoneJoinTrimTests)
add_executable(TailtoneReverbTests Tests/TailReverbTests.cpp)
add_test(NAME GeneratedTailReverbDSP COMMAND TailtoneReverbTests)
add_executable(TailtoneReverbPitchTests Tests/TailReverbPitchTests.cpp)
target_link_libraries(TailtoneReverbPitchTests PRIVATE pt_r8brain)
pt_sample_test(RootRenderedTailReverbPitch TailtoneReverbPitchTests tail.wav)
add_executable(TailtoneSubDuckTests Tests/TailSubDuckTests.cpp)
add_test(NAME GeneratedSubSidechain COMMAND TailtoneSubDuckTests)
add_executable(TailtoneAdaptiveStretchTests Tests/AdaptiveStretchTests.cpp)
target_link_libraries(TailtoneAdaptiveStretchTests PRIVATE pt_r8brain)
pt_sample_test(AdaptiveStretchDuration TailtoneAdaptiveStretchTests kick-nc-lol-2.0.wav)
add_executable(TailtoneLinearPhaseTests Tests/LinearPhaseTests.cpp)
add_test(NAME LinearPhaseWetEQ COMMAND TailtoneLinearPhaseTests)
# v0.5.10: optimized realtime DSP must stay bit-identical to the v0.5.9 code (embedded as reference).
add_executable(TailtoneCpuOptimizationTests Tests/CpuOptimizationTests.cpp)
add_test(NAME CpuOptimizationsBitIdentical COMMAND TailtoneCpuOptimizationTests)
add_executable(TailtoneTailWetMonoTests Tests/TailWetMonoTests.cpp)
add_test(NAME TailWetSubMono90Hz COMMAND TailtoneTailWetMonoTests)
add_executable(TailtonePunchRegionTests Tests/PunchRegionTests.cpp)
add_test(NAME PunchAttackDecayDSP COMMAND TailtonePunchRegionTests)
add_executable(TailtoneMasterEnvelopeTests Tests/MasterEnvelopeTests.cpp)
add_test(NAME CompleteWaveformADSR COMMAND TailtoneMasterEnvelopeTests)
add_executable(TailtoneTailEndCutTests Tests/TailEndCutTests.cpp)
target_link_libraries(TailtoneTailEndCutTests PRIVATE pt_r8brain)
add_test(NAME TailEndCutDryOnly COMMAND TailtoneTailEndCutTests)
add_executable(TailtonePitchDropTests Tests/PitchDropTests.cpp)
target_link_libraries(TailtonePitchDropTests PRIVATE pt_r8brain)
pt_sample_test(HighestFidelityPitchDrop TailtonePitchDropTests virus-kick-3.wav)
# v0.4.65: spectral join analysis and parallel note rendering.
add_executable(TailtoneSpectralJoinTests Tests/SpectralJoinTests.cpp)
add_test(NAME SpectralJoinView COMMAND TailtoneSpectralJoinTests)
add_executable(TailtoneParallelRenderTests Tests/ParallelRenderTests.cpp)
target_link_libraries(TailtoneParallelRenderTests PRIVATE pt_r8brain)
pt_sample_test(ParallelNoteRender TailtoneParallelRenderTests virus-kick-3.wav)
# v0.5.16 one preparation limit shared by every instance + background delay (portable).
add_executable(TailtonePreparationGateTests Tests/PreparationGateTests.cpp)
target_link_libraries(TailtonePreparationGateTests PRIVATE Threads::Threads)
add_test(NAME SharedPreparationLimit COMMAND TailtonePreparationGateTests)
# v0.4.68: Extreme v5 tail engine.
add_executable(TailtoneExtremeV5Tests Tests/ExtremeV5Tests.cpp)
target_link_libraries(TailtoneExtremeV5Tests PRIVATE pt_r8brain)
pt_sample_test(ExtremeV5Engine TailtoneExtremeV5Tests virus-kick-3.wav kick-nc-lol-2.0.wav tail.wav mpl-kick-merged.wav)   # v0.4.68: stub last cycle ("vector too long")
# v0.5.13: AutoSplitTailStart removed with AUTO-SPLIT KICK (user request).
if(PT_CORE_ONLY)
    return()
endif()
if(NOT TARGET juce::juce_audio_utils)
    # A partially extracted third-party JUCE folder can contain CMakeLists.txt
    # while its required CMake helpers are missing. Don't use it in that case.
    # A fresh, pinned official JUCE checkout is an automatic recovery option.
    if(NOT DEFINED JUCE_PATH)
        set(PT_BUNDLED_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE")
        if(EXISTS "${PT_BUNDLED_JUCE}/CMakeLists.txt" AND
           EXISTS "${PT_BUNDLED_JUCE}/extras/Build/CMake/JUCEModuleSupport.cmake" AND
           EXISTS "${PT_BUNDLED_JUCE}/extras/Build/CMake/JUCEUtils.cmake" AND
           EXISTS "${PT_BUNDLED_JUCE}/modules/juce_audio_utils/juce_audio_utils.h")
            set(JUCE_PATH "${PT_BUNDLED_JUCE}")
        else()
            message(WARNING "Bundled JUCE seems incomplete. Downloading the pinned official JUCE source instead. If this fails, extract the ZIP again to a shorter path, such as C:/Tailtone.")
        endif()
    endif()
    if(DEFINED JUCE_PATH AND NOT "${JUCE_PATH}" STREQUAL "")
        if(NOT EXISTS "${JUCE_PATH}/extras/Build/CMake/JUCEModuleSupport.cmake")
            message(FATAL_ERROR "JUCE_PATH is incomplete: ${JUCE_PATH}. Missing extras/Build/CMake/JUCEModuleSupport.cmake. Re-extract JUCE or remove -DJUCE_PATH to fetch it automatically.")
        endif()
        add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce")
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 4f43011b96eb0636104cb3e433894cda98243626)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()
juce_add_binary_data(TailtoneLogo SOURCES Assets/jeilogo.png)
# v0.4.61: macOS also builds an Audio Unit (Logic, GarageBand).
if(APPLE)
    set(TT_FORMATS VST3 AU Standalone)
else()
    set(TT_FORMATS VST3 Standalone)
endif()
# v0.5 PERMANENT IDENTITY. From v0.5 on every release is the same plugin, so a
# new version replaces the installed one and opens projects saved with any
# earlier v0.5+ release (and v0.4.69, whose codes these are). NEVER change the
# target name, PRODUCT_NAME, BUNDLE_ID, PLUGIN_MANUFACTURER_CODE or PLUGIN_CODE.
# The version lives only in project(VERSION) above. IdentityTests guards this.
juce_add_plugin(Tailtone
    COMPANY_NAME "Jei Audio" BUNDLE_ID "audio.jei.tailtone"
    PLUGIN_MANUFACTURER_CODE JeiA PLUGIN_CODE TaA2
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/tailtone-icon.png"
    COMPANY_COPYRIGHT "Jei Audio (Jeipster), GPLv3"
    FORMATS ${TT_FORMATS} PRODUCT_NAME "Tailtone"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Instrument Sampler
    VST3_AUTO_MANIFEST FALSE)

target_sources(Tailtone PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
# v0.5.16 RENDER KEY: the prepared notes saved in projects are keyed by a hash of
# the DSP sources instead of the version, so an update without rendering changes
# keeps them. Every Source/*.h except the UI / scheduling headers, plus r8brain;
# line endings are normalised. CONFIGURE_DEPENDS re-runs this when one changes,
# and file(CONFIGURE) rewrites the header only when the hash changed, so only
# PluginProcessor.cpp recompiles. Render glue in PluginProcessor.cpp/.h is keyed
# by kRenderFormat there (bump it by hand).
file(GLOB TT_RENDER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/r8brain/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/r8brain/*.inc")
list(FILTER TT_RENDER_SOURCES EXCLUDE REGEX "/(PluginEditor|PluginProcessor|ParallelRender|Theme|UserDefaults|EQGraph|KickOverview|KickExport|PunchRegionWaveform|WaveformEdit|WaveformEnvelope|SpectralJoin|ProcessedPreview|PreparationProgress|PunchSplitTrim)\\.h$")
list(SORT TT_RENDER_SOURCES)
set(TT_RENDER_HASHES "")
foreach(tt_file IN LISTS TT_RENDER_SOURCES)
    file(READ "${tt_file}" tt_content)
    string(REPLACE "\r\n" "\n" tt_content "${tt_content}")
    string(SHA256 tt_hash "${tt_content}")
    get_filename_component(tt_name "${tt_file}" NAME)
    string(APPEND TT_RENDER_HASHES "${tt_name}:${tt_hash};")
endforeach()
string(SHA256 TT_RENDER_HASH "${TT_RENDER_HASHES}")
string(SUBSTRING "${TT_RENDER_HASH}" 0 16 TT_RENDER_SOURCE_KEY)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/RenderSourceKey.h"
     CONTENT "// Generated by CMakeLists.txt (v0.5.16 render key). Do not edit.\n#pragma once\n#define TAILTONE_RENDER_SOURCE_KEY 0x@TT_RENDER_SOURCE_KEY@ull\n" @ONLY)
target_include_directories(Tailtone PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Avoid Windows <windef.h> min/max macros in JUCE, r8brain and plugin DSP.
# Also protect our own calls using (std::min)/(std::max) so this remains
# robust even if another Windows header defines the macros unexpectedly.
target_compile_definitions(Tailtone PUBLIC NOMINMAX)
target_compile_definitions(Tailtone PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(Tailtone PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC pt_r8brain juce::juce_recommended_config_flags TailtoneLogo)   # v0.5.5: PluginProcessor.h now reaches PunchPitch.h (r8brain)
if(MINGW)
    target_link_options(Tailtone_VST3 PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(Tailtone_Standalone PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(PunchTailDistortionDSPTests Tests/DistortionDSPTests.cpp)
target_compile_definitions(PunchTailDistortionDSPTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(PunchTailDistortionDSPTests PRIVATE $<TARGET_PROPERTY:juce::juce_dsp,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(PunchTailDistortionDSPTests PRIVATE Tailtone juce::juce_dsp)
add_test(NAME IndependentDistortionDSP COMMAND PunchTailDistortionDSPTests)

add_executable(PunchTailPlaybackTests Tests/PlaybackTests.cpp)
# Link the already-built processor/JUCE implementation instead of compiling
# JUCE a second time just for the integration test.
target_compile_definitions(PunchTailPlaybackTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(PunchTailPlaybackTests PRIVATE
    $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(PunchTailPlaybackTests PRIVATE Tailtone pt_r8brain)
add_test(NAME PlaybackContinuityAndState COMMAND PunchTailPlaybackTests)

add_executable(PunchTailEditorTests Tests/EditorTests.cpp)
target_compile_definitions(PunchTailEditorTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(PunchTailEditorTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(PunchTailEditorTests PRIVATE Tailtone pt_r8brain)
add_test(NAME WaveformMouseAndState COMMAND PunchTailEditorTests)

add_executable(TailtoneProcessedPreviewTests Tests/ProcessedPreviewTests.cpp)
target_compile_definitions(TailtoneProcessedPreviewTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneProcessedPreviewTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneProcessedPreviewTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME ProcessedWaveformPreview COMMAND TailtoneProcessedPreviewTests)

add_executable(TailtoneSilenceStateTests Tests/SilenceStateTests.cpp)
target_compile_definitions(TailtoneSilenceStateTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneSilenceStateTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneSilenceStateTests PRIVATE Tailtone pt_r8brain)
add_test(NAME SilenceTrimWorkerAndState COMMAND TailtoneSilenceStateTests)

add_executable(TailtoneReverbIntegrationTests Tests/TailReverbIntegrationTests.cpp)
target_compile_definitions(TailtoneReverbIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneReverbIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneReverbIntegrationTests PRIVATE Tailtone pt_r8brain)
add_test(NAME GeneratedReverbExactNoteCutoff COMMAND TailtoneReverbIntegrationTests)

add_executable(TailtoneLinearIntegrationTests Tests/LinearIntegrationTests.cpp)
target_compile_definitions(TailtoneLinearIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneLinearIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneLinearIntegrationTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
pt_sample_test(AdaptiveAndLinearPhaseIntegration TailtoneLinearIntegrationTests kick-nc-lol-2.0.wav)

add_executable(TailtonePunchRegionIntegrationTests Tests/PunchRegionIntegrationTests.cpp)
target_compile_definitions(TailtonePunchRegionIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtonePunchRegionIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtonePunchRegionIntegrationTests PRIVATE Tailtone pt_r8brain)
add_test(NAME PunchAttackDecayIntegration COMMAND TailtonePunchRegionIntegrationTests)

add_executable(TailtoneMasterEnvelopeIntegrationTests Tests/MasterEnvelopeIntegrationTests.cpp)
target_compile_definitions(TailtoneMasterEnvelopeIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneMasterEnvelopeIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneMasterEnvelopeIntegrationTests PRIVATE Tailtone pt_r8brain)
add_test(NAME CompleteWaveformADSRIntegration COMMAND TailtoneMasterEnvelopeIntegrationTests)

add_executable(TailtoneManualRootTests Tests/ManualRootFallbackTests.cpp)
target_compile_definitions(TailtoneManualRootTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneManualRootTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneManualRootTests PRIVATE Tailtone pt_r8brain)
add_test(NAME ManualTailRootFallback COMMAND TailtoneManualRootTests)

# v0.4.54 Kick Character engine
add_executable(TailtoneKickCharacterTests Tests/KickCharacterTests.cpp)
target_link_libraries(TailtoneKickCharacterTests PRIVATE pt_r8brain)
pt_sample_test(KickCharacterEngine TailtoneKickCharacterTests virus-kick-3.wav kick-nc-lol-2.0.wav tail.wav)
add_executable(TailtoneKickCharacterIntegrationTests Tests/KickCharacterIntegrationTests.cpp)
target_compile_definitions(TailtoneKickCharacterIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneKickCharacterIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneKickCharacterIntegrationTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(KickCharacterIntegration TailtoneKickCharacterIntegrationTests virus-kick-3.wav)
add_executable(TailtoneKeyMappingTests Tests/KeyMappingTests.cpp)
target_compile_definitions(TailtoneKeyMappingTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneKeyMappingTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneKeyMappingTests PRIVATE Tailtone pt_r8brain)
add_test(NAME KeyOctaveShiftAndLowPunchLane COMMAND TailtoneKeyMappingTests)
add_executable(TailtoneGlideLayerTests Tests/GlideLayerTests.cpp)
target_compile_definitions(TailtoneGlideLayerTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneGlideLayerTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneGlideLayerTests PRIVATE Tailtone pt_r8brain)
add_test(NAME LegatoGlideAndPunchLayer COMMAND TailtoneGlideLayerTests)
add_executable(TailtoneFormantDuckTests Tests/FormantDuckTests.cpp)
target_compile_definitions(TailtoneFormantDuckTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneFormantDuckTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneFormantDuckTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(CrunchLayerAndPunchReverbDuck TailtoneFormantDuckTests virus-kick-3.wav tail.wav)
add_executable(TailtoneRootRenderPhaseTests Tests/RootRenderPhaseTests.cpp)
target_compile_definitions(TailtoneRootRenderPhaseTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneRootRenderPhaseTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneRootRenderPhaseTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(RootRenderAutoMatchPhase TailtoneRootRenderPhaseTests punch.wav tail.wav)
add_executable(TailtoneWetAlignTests Tests/WetAlignTests.cpp)
target_compile_definitions(TailtoneWetAlignTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneWetAlignTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneWetAlignTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(RootRenderWetAutoMatchAndPhaseLock TailtoneWetAlignTests tail.wav virus-kick-3.wav)

add_executable(TailtonePitchDropIntegrationTests Tests/PitchDropIntegrationTests.cpp)
target_compile_definitions(TailtonePitchDropIntegrationTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtonePitchDropIntegrationTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtonePitchDropIntegrationTests PRIVATE Tailtone pt_r8brain)
add_test(NAME HighestFidelityPitchDropIntegration COMMAND TailtonePitchDropIntegrationTests)

# v0.4.60 optional output limiter
add_executable(TailtoneOutputLimiterTests Tests/OutputLimiterTests.cpp)
target_compile_definitions(TailtoneOutputLimiterTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneOutputLimiterTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneOutputLimiterTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME OutputLimiter COMMAND TailtoneOutputLimiterTests)

# v0.4.62 attack layer
# v0.4.65 AUTO-SPLIT (worker, state, refusal) on the difficult kick fixture.
# v0.5.15: played keys first, prepared notes saved in the project.
add_executable(TailtonePlayedKeysTests Tests/PlayedKeysTests.cpp)
target_compile_definitions(TailtonePlayedKeysTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtonePlayedKeysTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtonePlayedKeysTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(PlayedKeysFirst TailtonePlayedKeysTests punch.wav tail.wav)
# v0.5.19 preparation progress bar (time left until all 37 keys are ready).
add_executable(TailtonePreparationProgressTests Tests/PreparationProgressTests.cpp)
target_compile_definitions(TailtonePreparationProgressTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtonePreparationProgressTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtonePreparationProgressTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(PreparationProgress TailtonePreparationProgressTests punch.wav tail.wav virus-kick-3.wav)
# v0.5.13: AutoSplitKick replaced by LockSplitPair (AUTO-SPLIT KICK removed, user request).
add_executable(TailtoneSplitPairTests Tests/SplitPairTests.cpp)
target_compile_definitions(TailtoneSplitPairTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneSplitPairTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneSplitPairTests PRIVATE Tailtone pt_r8brain)
pt_sample_test(LockSplitPair TailtoneSplitPairTests virus-kick-3.wav kick-nc-lol-2.0.wav)
add_executable(TailtoneAttackLayerTests Tests/AttackLayerTests.cpp)
target_compile_definitions(TailtoneAttackLayerTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneAttackLayerTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneAttackLayerTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME AttackLayer COMMAND TailtoneAttackLayerTests)

# v0.5 permanent identity (name, codes, bundle ID, state across versions).
add_executable(TailtoneIdentityTests Tests/IdentityTests.cpp)
target_compile_definitions(TailtoneIdentityTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneIdentityTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneIdentityTests PRIVATE Tailtone pt_r8brain)
add_test(NAME PermanentIdentity COMMAND TailtoneIdentityTests)
# v0.5 MONO BASS (DSP, processor, state, preview).
add_executable(TailtoneBassMonoTests Tests/BassMonoTests.cpp)
target_compile_definitions(TailtoneBassMonoTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneBassMonoTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneBassMonoTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME MonoBass COMMAND TailtoneBassMonoTests)
# v0.5.20 EQ POSITION (Punch/Tail EQ before or after the distortion).
add_executable(TailtoneEqPositionTests Tests/EqPositionTests.cpp)
target_compile_definitions(TailtoneEqPositionTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneEqPositionTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneEqPositionTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME EqPosition COMMAND TailtoneEqPositionTests)
# v0.5.21 ATTACK / DECAY NORMALIZE: Punch attack / decay output trims.
add_executable(TailtoneSplitNormalizeTests Tests/SplitNormalizeTests.cpp)
target_compile_definitions(TailtoneSplitNormalizeTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(TailtoneSplitNormalizeTests PRIVATE $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(TailtoneSplitNormalizeTests PRIVATE Tailtone pt_r8brain juce::juce_dsp)
add_test(NAME AttackDecayNormalize COMMAND TailtoneSplitNormalizeTests)
