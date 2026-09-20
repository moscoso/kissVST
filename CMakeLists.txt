cmake_minimum_required(VERSION 3.22)

project(Kiss VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is checked out in a sibling folder (C:/Users/chris/code/JUCE).
# Point elsewhere with:  cmake -B build -DJUCE_DIR=path/to/JUCE
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to the JUCE framework checkout")
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

# After each build, copy the .vst3 into C:/Program Files/Common Files/VST3 so Ableton picks it up.
option(KISS_COPY_PLUGIN "Copy the built VST3 into the system VST3 folder after each build" ON)

juce_add_plugin(Kiss
    PRODUCT_NAME "Kiss"
    COMPANY_NAME "Chris"
    PLUGIN_MANUFACTURER_CODE Chrs      # 4 chars, first one uppercase
    PLUGIN_CODE Kiss                   # 4 chars, must be unique per plugin
    FORMATS VST3 Standalone            # Standalone = a plain .exe you can run without a DAW
    VST3_CATEGORIES Fx Delay Reverb
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${KISS_COPY_PLUGIN})

target_sources(Kiss PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Kiss PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Kiss
    PRIVATE
        juce::juce_audio_utils         # AudioProcessor, editor, standalone wrapper
        juce::juce_dsp                 # DelayLine, Reverb
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
