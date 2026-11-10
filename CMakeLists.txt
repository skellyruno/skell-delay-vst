cmake_minimum_required(VERSION 3.22)
project(DelayPlugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(DelayPlugin
    COMPANY_NAME "Skelly Audio"
    BUNDLE_ID com.skellyaudio.skelldelay
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE      # preset search, typed values and Ctrl/Cmd+Z need keystrokes
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Skly
    PLUGIN_CODE Dlay
    FORMATS VST3 AU
    PRODUCT_NAME "Skell-Delay"
)

juce_generate_juce_header(DelayPlugin)

# FIX: Disable legacy VST2 preset compatibility in the VST3 wrapper
target_compile_definitions(DelayPlugin
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_sources(DelayPlugin
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/Presets.h
        Source/GatePatterns.h
        Source/OnsetDetector.h
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/SkellLookAndFeel.h
        Source/SkellComponents.h
        Source/SkellUi.h
        Source/PresetBrowser.h
)

# Add BinaryData for assets
juce_add_binary_data(DelayAssets
    SOURCES
        Source/Assets/skell_bg.png
)

target_link_libraries(DelayPlugin
    PRIVATE
        DelayAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- optional: the headless test programs (tests/). Configure with -DSKELL_BUILD_TESTS=ON, then: ctest --test-dir build -C Release
# (written and run with a hand-made harness; this CMake target itself has not been built yet - see docs/RELEASE_CHECKLIST.md)
option(SKELL_BUILD_TESTS "Build the DSP / stress / real-time test programs" OFF)
if(SKELL_BUILD_TESTS)
    enable_testing()
    foreach(t dsp_tests stress_tests realtime_check)
        juce_add_console_app(${t} PRODUCT_NAME ${t})
        juce_generate_juce_header(${t})
        target_sources(${t} PRIVATE tests/${t}.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_include_directories(${t} PRIVATE Source)
        target_compile_definitions(${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${t} PRIVATE DelayAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
        add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endforeach()
endif()
