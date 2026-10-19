cmake_minimum_required(VERSION 3.22)

project(SmartKeys VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")

# Visual Studio: los textos de la interfaz (ñ, á, ●, ▶, ♭...) están en UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Universal binary (Apple Silicon + Intel) por defecto en macOS
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(SMARTKEYS_BUILD_TESTS "Compilar las pruebas del motor DSP" OFF)
option(SMARTKEYS_BUILD_FX "Compilar también SmartKeys FX (efecto para la pista del micrófono)" ON)
option(SMARTKEYS_COPY_AFTER_BUILD
       "Copiar los plugins a las carpetas de plugins del usuario al compilar (borra antes la versión anterior)" OFF)

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
# Carpetas de plugins del usuario (no requieren permisos de administrador)
#-------------------------------------------------------------------------------
if(APPLE)
    set(SMARTKEYS_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    set(SMARTKEYS_AU_DIR   "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
elseif(WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/VST3" SMARTKEYS_VST3_DIR)
    set(SMARTKEYS_AU_DIR "")
else()
    set(SMARTKEYS_VST3_DIR "$ENV{HOME}/.vst3")
    set(SMARTKEYS_AU_DIR "")
endif()

set(SMARTKEYS_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

#-------------------------------------------------------------------------------
# smartkeys_add_plugin(): crea una variante del plugin a partir del mismo código
#-------------------------------------------------------------------------------
function(smartkeys_add_plugin target)
    cmake_parse_arguments(SK "" "PRODUCT;CODE;BUNDLE;IS_SYNTH;AU_TYPE;FX_VARIANT;DESCRIPTION" "FORMATS;CATEGORIES" ${ARGN})

    set(copy_args VST3_COPY_DIR "${SMARTKEYS_VST3_DIR}")
    if(APPLE)
        list(APPEND copy_args AU_COPY_DIR "${SMARTKEYS_AU_DIR}")
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME                  "JeanPaul Audio"
        COMPANY_WEBSITE               ""
        BUNDLE_ID                     ${SK_BUNDLE}
        PLUGIN_MANUFACTURER_CODE      Jpau
        PLUGIN_CODE                   ${SK_CODE}
        PRODUCT_NAME                  "${SK_PRODUCT}"
        DESCRIPTION                   "${SK_DESCRIPTION}"
        VERSION                       ${PROJECT_VERSION}
        FORMATS                       ${SK_FORMATS}

        IS_SYNTH                      ${SK_IS_SYNTH}
        NEEDS_MIDI_INPUT              TRUE
        NEEDS_MIDI_OUTPUT             TRUE
        IS_MIDI_EFFECT                FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS   FALSE

        VST3_CATEGORIES               ${SK_CATEGORIES}
        AU_MAIN_TYPE                  ${SK_AU_TYPE}
        AU_SANDBOX_SAFE               TRUE

        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT    "SmartKeys usa el micrófono para convertir tu voz en notas y acordes MIDI en tiempo real."
        HARDENED_RUNTIME_ENABLED      TRUE
        HARDENED_RUNTIME_OPTIONS      com.apple.security.device.audio-input

        COPY_PLUGIN_AFTER_BUILD       ${SMARTKEYS_COPY_AFTER_BUILD}
        ${copy_args})

    target_sources(${target} PRIVATE ${SMARTKEYS_SOURCES})
    target_include_directories(${target} PRIVATE Source)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        SMARTKEYS_FX_VARIANT=${SK_FX_VARIANT})

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Instalación local: borrar la versión anterior ANTES de copiar la nueva
    if(SMARTKEYS_COPY_AFTER_BUILD)
        if(TARGET ${target}_VST3)
            add_custom_command(TARGET ${target}_VST3 PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMARTKEYS_VST3_DIR}/${SK_PRODUCT}.vst3"
                COMMENT "SmartKeys: borrando ${SK_PRODUCT}.vst3 anterior de ${SMARTKEYS_VST3_DIR}"
                VERBATIM)
        endif()

        if(APPLE AND TARGET ${target}_AU)
            add_custom_command(TARGET ${target}_AU PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMARTKEYS_AU_DIR}/${SK_PRODUCT}.component"
                COMMENT "SmartKeys: borrando ${SK_PRODUCT}.component anterior de ${SMARTKEYS_AU_DIR}"
                VERBATIM)
            add_custom_command(TARGET ${target}_AU POST_BUILD
                COMMAND sh -c "killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true"
                COMMENT "SmartKeys: refrescando la caché de Audio Units"
                VERBATIM)
        endif()
    endif()
endfunction()

#-------------------------------------------------------------------------------
# Variante 1 — SmartKeys (instrumento: Channel Rack de FL / pista MIDI de Ableton)
#   Entrada de micrófono como sidechain (como un vocoder).
#-------------------------------------------------------------------------------
smartkeys_add_plugin(SmartKeys
    PRODUCT      "SmartKeys"
    CODE         Smk1
    BUNDLE       com.jeanpaulaudio.smartkeys
    IS_SYNTH     TRUE
    AU_TYPE      kAudioUnitType_MusicDevice
    FX_VARIANT   0
    DESCRIPTION  "Smart Piano, Chord Generator & Live Voice Controller"
    FORMATS      VST3 AU Standalone
    CATEGORIES   Instrument Synth Generator)

#-------------------------------------------------------------------------------
# Variante 2 — SmartKeys FX (efecto: se inserta en la pista del micrófono del
#   Mixer y recibe la voz como entrada principal, sin ruteos)
#-------------------------------------------------------------------------------
if(SMARTKEYS_BUILD_FX)
    smartkeys_add_plugin(SmartKeysFX
        PRODUCT      "SmartKeys FX"
        CODE         Smkf
        BUNDLE       com.jeanpaulaudio.smartkeysfx
        IS_SYNTH     FALSE
        AU_TYPE      kAudioUnitType_MusicEffect
        FX_VARIANT   1
        DESCRIPTION  "SmartKeys voice-to-chords effect for the microphone track"
        FORMATS      VST3 AU
        CATEGORIES   Fx Generator)
endif()

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
            juce::juce_audio_devices
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

#-------------------------------------------------------------------------------
# Prueba de integración: carga los VST3 compilados como un DAW y comprueba la
# salida MIDI (acordes completos, canal, STRUM) y la voz por la entrada.
#   cmake -DSMARTKEYS_BUILD_HOST_TESTS=ON ... && cmake --build . --target SmartKeysHostTests
#   SmartKeysHostTests <SmartKeys.vst3> [<SmartKeys FX.vst3>]
#-------------------------------------------------------------------------------
option(SMARTKEYS_BUILD_HOST_TESTS "Compilar la prueba de integración que carga los VST3" OFF)
if(SMARTKEYS_BUILD_HOST_TESTS)
    juce_add_console_app(SmartKeysHostTests PRODUCT_NAME "SmartKeysHostTests")
    target_sources(SmartKeysHostTests PRIVATE Tests/HostTests.cpp)
    target_compile_definitions(SmartKeysHostTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SmartKeysHostTests
        PRIVATE
            juce::juce_audio_processors_headless
            juce::juce_events
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
