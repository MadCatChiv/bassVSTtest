cmake_minimum_required(VERSION 3.22)
project(BassDriver VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(BassDriver
    COMPANY_NAME "BassTools"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Bsst
    PLUGIN_CODE Bsdr
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Bass Driver"
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(BassDriver PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_features(BassDriver PRIVATE cxx_std_17)

target_compile_definitions(BassDriver PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BassDriver
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
