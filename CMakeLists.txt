cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), runs on macOS 10.13+.
# Must be set before project(); override on the command line if needed.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(DrumSpread VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DS_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone)" ON)
option(DS_BUILD_TESTS  "Build the offline verification harness" ON)

# Static MSVC runtime so the .vst3 / .exe have no VC++ redistributable dependency
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# dscore: JUCE-free DSP core shared by the plugin and the test harness
# ---------------------------------------------------------------------------
add_library(dscore STATIC
    Source/dsp/Engine.cpp
    Source/dsp/FactoryPresets.cpp
    Source/dsp/FFT.cpp
    Source/dsp/HeatModule.cpp
    Source/dsp/LayerModule.cpp
    Source/dsp/MasterSection.cpp
    Source/dsp/Oversampler.cpp
    Source/dsp/Params.cpp
    Source/dsp/PressModule.cpp
    Source/dsp/RoomModule.cpp
    Source/dsp/SmashModule.cpp
    Source/dsp/WarpModule.cpp)
target_include_directories(dscore PUBLIC Source/dsp)
set_target_properties(dscore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(dscore PRIVATE /W4 /fp:precise /permissive- /wd4100)
else()
    target_compile_options(dscore PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Offline verification harness (JUCE-free)
# ---------------------------------------------------------------------------
if(DS_BUILD_TESTS)
    add_executable(dscore_tests
        tests/TestMain.cpp
        tests/TestUtil.cpp
        tests/TestSignals.cpp
        tests/TestModules.cpp
        tests/TestEngine.cpp)
    target_link_libraries(dscore_tests PRIVATE dscore)
    target_include_directories(dscore_tests PRIVATE tests)
    if(MSVC)
        target_compile_options(dscore_tests PRIVATE /W3 /fp:precise /bigobj)
    endif()
endif()

# ---------------------------------------------------------------------------
# JUCE plugin
# ---------------------------------------------------------------------------
if(DS_BUILD_PLUGIN)
    include(FetchContent)
    # Pinned JUCE release. Override with -DFETCHCONTENT_SOURCE_DIR_JUCE=<path> to
    # use a local checkout.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    # Audio Unit is added on macOS (Logic Pro, GarageBand need it)
    set(DS_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND DS_FORMATS AU)
    endif()

    juce_add_plugin(DrumSpread
        COMPANY_NAME "DrumSpread Audio"
        COMPANY_CODE "DsAu"
        PLUGIN_MANUFACTURER_CODE "DsAu"
        PLUGIN_CODE "DsPr"
        PRODUCT_NAME "DrumSpread"
        BUNDLE_ID "com.drumspread.drumspread"
        FORMATS ${DS_FORMATS}
        AU_MAIN_TYPE kAudioUnitType_Effect
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Dynamics Distortion
        VST3_AUTO_MANIFEST FALSE)

    set(DS_PLUGIN_SOURCES
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PresetManager.cpp
        Source/ui/PluginEditor.cpp
        Source/ui/LookAndFeel.cpp
        Source/ui/ModulePanel.cpp
        Source/ui/Displays.cpp)

    target_sources(DrumSpread PRIVATE ${DS_PLUGIN_SOURCES})
    target_include_directories(DrumSpread PRIVATE Source/plugin Source/ui)

    set(DS_JUCE_DEFS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_compile_definitions(DrumSpread PUBLIC ${DS_JUCE_DEFS})

    target_link_libraries(DrumSpread
        PRIVATE
            dscore
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Plugin-level tests (state, presets, latency, bus layouts, RT allocations, UI snapshot)
    if(DS_BUILD_TESTS)
        juce_add_console_app(plugin_tests PRODUCT_NAME "DrumSpreadPluginTests")
        target_sources(plugin_tests PRIVATE tests/PluginTests.cpp ${DS_PLUGIN_SOURCES})
        target_include_directories(plugin_tests PRIVATE Source/plugin Source/ui tests)
        target_compile_definitions(plugin_tests PRIVATE
            ${DS_JUCE_DEFS}
            JucePlugin_Name="DrumSpread"
            JucePlugin_VersionString="1.0.0"
            JucePlugin_IsSynth=0
            JucePlugin_WantsMidiInput=0
            JucePlugin_ProducesMidiOutput=0
            JucePlugin_IsMidiEffect=0
            DS_PLUGIN_TESTS=1)
        target_link_libraries(plugin_tests PRIVATE
            dscore
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
    endif()
endif()
