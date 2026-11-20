# Sixteen: circuit model of the classic 16-channel analog vocoder (VST3 effect with side-chain)

add_library(sixteen_dsp STATIC dsp/VocoderEngine.cpp)
target_include_directories(sixteen_dsp PUBLIC dsp)
target_compile_features(sixteen_dsp PUBLIC cxx_std_17)
set_target_properties(sixteen_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(sixteen_dsp PRIVATE /W4)
else()
    target_compile_options(sixteen_dsp PRIVATE -Wall -Wextra)
endif()

if(FILAMENT_BUILD_TESTS)
    add_executable(sixteen_test tests/vocoder_test.cpp)
    target_link_libraries(sixteen_test PRIVATE sixteen_dsp)
    add_test(NAME sixteen_vocoder_test COMMAND sixteen_test ${CMAKE_BINARY_DIR})
endif()

if(TARGET juce::juce_audio_utils)
    juce_add_plugin(Sixteen
        COMPANY_NAME             "My VST Experiments"
        PLUGIN_MANUFACTURER_CODE Mvex
        PLUGIN_CODE              Sxtn
        PRODUCT_NAME             "Sixteen"
        BUNDLE_ID                com.myvstexperiments.sixteen
        FORMATS                  VST3 Standalone
        IS_SYNTH                 FALSE
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD  FALSE
        VST3_CATEGORIES          Fx Filter)

    target_sources(Sixteen PRIVATE
        plugin/SixteenProcessor.cpp
        plugin/SixteenEditor.cpp)

    target_compile_definitions(Sixteen PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(Sixteen
        PRIVATE
            sixteen_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# Developer tool: render the editor to a PNG (cmake -DSIXTEEN_BUILD_SCREENSHOT=ON)
option(SIXTEEN_BUILD_SCREENSHOT "Build the editor screenshot tool" OFF)
if(SIXTEEN_BUILD_SCREENSHOT AND TARGET juce::juce_audio_utils)
    juce_add_console_app(sixteen_screenshot PRODUCT_NAME "sixteen_screenshot")
    target_sources(sixteen_screenshot PRIVATE tools/screenshot.cpp plugin/SixteenProcessor.cpp plugin/SixteenEditor.cpp)
    target_include_directories(sixteen_screenshot PRIVATE plugin)
    target_compile_definitions(sixteen_screenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Sixteen")
    target_link_libraries(sixteen_screenshot PRIVATE sixteen_dsp juce::juce_audio_utils)
endif()
