cmake_minimum_required(VERSION 3.22)
project(RiserKnob VERSION 3.2.0)

option(COPY_PLUGIN "Plugin nach dem Build in den System-Ordner kopieren" ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(RiserKnob
    COMPANY_NAME "HouseTools"
    PLUGIN_MANUFACTURER_CODE Hstl
    PLUGIN_CODE Rsk1
    FORMATS VST3 AU
    PRODUCT_NAME "Riser Knob"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN}
)

target_sources(RiserKnob PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(RiserKnob PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(RiserKnob
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
