cmake_minimum_required(VERSION 3.24)
project(Open5150 VERSION 0.1.0 LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "The shared DSP currently requires macOS Accelerate and AudioToolbox.")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/8.0.12
    URL_HASH SHA256=dec1a8baee5aaec4502b717a421b850e257ab7e9c2efd03766185ec1957cf102
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Open5150
    COMPANY_NAME "OPEN5150"
    BUNDLE_ID audio.open5150.plugin
    PLUGIN_MANUFACTURER_CODE Opam
    PLUGIN_CODE Ofiv
    FORMATS AU VST3
    PRODUCT_NAME "OPEN5150"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion)
target_sources(Open5150 PRIVATE plugin/PluginProcessor.cpp)
target_compile_features(Open5150 PUBLIC cxx_std_17)
target_compile_definitions(Open5150 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Open5150 PRIVATE
    juce::juce_audio_utils
    "-framework Accelerate" "-framework AudioToolbox"
    PUBLIC juce::juce_recommended_config_flags)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(Open5150PluginTests PRODUCT_NAME "OPEN5150 Plugin Tests")
    target_sources(Open5150PluginTests PRIVATE tests/test_plugin.cpp plugin/PluginProcessor.cpp)
    target_compile_features(Open5150PluginTests PRIVATE cxx_std_17)
    target_link_libraries(Open5150PluginTests PRIVATE juce::juce_audio_utils
        "-framework Accelerate" "-framework AudioToolbox")
    target_compile_definitions(Open5150PluginTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1)
    target_compile_options(Open5150PluginTests PRIVATE -UNDEBUG)
    add_dependencies(Open5150PluginTests Open5150_AU Open5150_VST3)
    add_test(NAME plugin COMMAND Open5150PluginTests)
    add_test(NAME plugin_bundles COMMAND Open5150PluginTests
        "$<TARGET_BUNDLE_DIR:Open5150_AU>" "$<TARGET_BUNDLE_DIR:Open5150_VST3>")
endif()
