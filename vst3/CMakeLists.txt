# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 DAW Plugin Capture contributors
#
# DAW Plugin Capture - VST3 effect. Put it on the master track to mirror the DAW's plugin windows into OBS.
#
#   cmake -S vst3 -B vst3/build -A x64 [-DJUCE_DIR=path/to/JUCE]
#   cmake --build vst3/build --config Release
#
# Without JUCE_DIR, the pinned JUCE release is downloaded at configure time.

cmake_minimum_required(VERSION 3.22)
project(DAWPluginCapture VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ redistributable needed

set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(JUCE_GIT_TAG "9.0.6" CACHE STRING "JUCE release to download when JUCE_DIR is not set")
set(PROJECT_URL "https://github.com/SyrantMusic/daw-plugin-capture" CACHE STRING
    "Source repository URL, shown in the plugin's legal notice")

if (JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_GIT_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DAWPluginCapture
    PRODUCT_NAME "DAW Plugin Capture"
    COMPANY_NAME "DAW Plugin Capture contributors"
    COMPANY_COPYRIGHT "Copyright (C) 2026 DAW Plugin Capture contributors. Licensed under the GNU AGPL v3 or later."
    BUNDLE_ID "io.github.dawplugincapture"
    PLUGIN_MANUFACTURER_CODE Dwpc
    PLUGIN_CODE Dpc1
    FORMATS VST3
    VST3_CATEGORIES Fx Tools
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DAWPluginCapture PRIVATE
    Source/CaptureEngine.cpp
    Source/ObsClient.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Settings.cpp
    Source/TerminalLookAndFeel.cpp)

target_compile_definitions(DAWPluginCapture
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        DAW_PLUGIN_CAPTURE_URL="${PROJECT_URL}")

if (MSVC)
    target_compile_options(DAWPluginCapture PRIVATE /utf-8)
endif()

target_link_libraries(DAWPluginCapture
    PRIVATE
        juce::juce_audio_processors
        juce::juce_cryptography
        crypt32 dwmapi winmm
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
