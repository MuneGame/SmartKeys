cmake_minimum_required(VERSION 3.22)

project(SmartKeys VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")

# Universal binary (Apple Silicon + Intel) por defecto en macOS
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(SMARTKEYS_BUILD_TESTS "Compilar las pruebas del motor DSP" OFF)

#-------------------------------------------------------------------------------
# JUCE: usa una copia local si se pasa -DJUCE_DIR=/ruta/a/JUCE; si no, la descarga
#-------------------------------------------------------------------------------
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#-------------------------------------------------------------------------------
# Plugin
#-------------------------------------------------------------------------------
juce_add_plugin(SmartKeys
    COMPANY_NAME                  "JeanPaul Audio"
    COMPANY_WEBSITE               ""
    BUNDLE_ID                     "com.jeanpaulaudio.smartkeys"
    PLUGIN_MANUFACTURER_CODE      Jpau
    PLUGIN_CODE                   Smk1
    PRODUCT_NAME                  "SmartKeys"
    DESCRIPTION                   "Smart Piano, Chord Generator & Live Voice Controller"
    FORMATS                       VST3 AU Standalone

    IS_SYNTH                      TRUE
    NEEDS_MIDI_INPUT              TRUE
    NEEDS_MIDI_OUTPUT             TRUE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE

    VST3_CATEGORIES               Instrument Synth
    AU_MAIN_TYPE                  kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE               TRUE

    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "SmartKeys usa el micrófono para convertir tu voz en notas y acordes MIDI en tiempo real."
    HARDENED_RUNTIME_ENABLED      TRUE
    HARDENED_RUNTIME_OPTIONS      com.apple.security.device.audio-input

    COPY_PLUGIN_AFTER_BUILD       FALSE)

juce_generate_juce_header(SmartKeys)

target_sources(SmartKeys PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(SmartKeys PRIVATE Source)

target_compile_definitions(SmartKeys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SmartKeys
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#-------------------------------------------------------------------------------
# Pruebas del motor (cmake -DSMARTKEYS_BUILD_TESTS=ON)
#-------------------------------------------------------------------------------
if(SMARTKEYS_BUILD_TESTS)
    juce_add_console_app(SmartKeysTests PRODUCT_NAME "SmartKeysTests")
    target_sources(SmartKeysTests PRIVATE Tests/EngineTests.cpp)
    target_include_directories(SmartKeysTests PRIVATE Source)
    target_compile_definitions(SmartKeysTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SmartKeysTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
