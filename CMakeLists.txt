cmake_minimum_required(VERSION 3.22)

project(Filament VERSION 0.7.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the .vst3 then works on any Windows machine without
# installing the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(FILAMENT_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone)" ON)
option(FILAMENT_BUILD_TESTS  "Build the offline DSP test renderer" ON)
set(FILAMENT_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout (otherwise JUCE is downloaded)")

# ---------------------------------------------------------------------------
# Framework-independent DSP engine
add_library(filament_dsp STATIC
    dsp/Voice.cpp
    dsp/Engine.cpp)
target_include_directories(filament_dsp PUBLIC dsp)
set_target_properties(filament_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(filament_dsp PRIVATE /W4)
else()
    target_compile_options(filament_dsp PRIVATE -Wall -Wextra)
endif()

if(FILAMENT_BUILD_TESTS)
    enable_testing()
    add_executable(filament_render_test tests/render_test.cpp)
    target_link_libraries(filament_render_test PRIVATE filament_dsp)
    add_test(NAME dsp_render_test COMMAND filament_render_test ${CMAKE_BINARY_DIR})
endif()

# ---------------------------------------------------------------------------
# Plugin
if(FILAMENT_BUILD_PLUGIN)
    if(FILAMENT_JUCE_PATH)
        add_subdirectory(${FILAMENT_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.10
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(Filament
        COMPANY_NAME             "My VST Experiments"
        PLUGIN_MANUFACTURER_CODE Mvex
        PLUGIN_CODE              Flmt
        PRODUCT_NAME             "Filament"
        FORMATS                  VST3 Standalone
        IS_SYNTH                 TRUE
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD  FALSE
        VST3_CATEGORIES          Instrument Synth)

    target_sources(Filament PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp)

    target_compile_definitions(Filament PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(Filament
        PRIVATE
            filament_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
