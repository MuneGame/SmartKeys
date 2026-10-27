cmake_minimum_required(VERSION 3.22)

project(SmartKeys VERSION 2.0.0 LANGUAGES C CXX)

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
option(SMARTKEYS_COPY_AFTER_BUILD
       "Copiar el plugin a las carpetas de plugins del usuario al compilar (borra antes la versión anterior)" OFF)

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

# Lectura directa del micrófono del sistema (CoreAudio en macOS; simulada en Linux)
set(SMARTKEYS_MIC_SOURCES Source/SystemMicInput.cpp)
if(APPLE)
    list(APPEND SMARTKEYS_MIC_SOURCES Source/MicPermission_mac.mm)
endif()

# Teclado MIDI virtual "SmartKeys Chords" (CoreMIDI en macOS; ALSA o archivo de prueba en Linux)
set(SMARTKEYS_PORT_SOURCES Source/ChordMidiPort.cpp)

# 2.0: ranura de instrumento (carga VST3 en todos los sistemas y Audio Unit en macOS)
set(SMARTKEYS_HOST_SOURCES Source/HostedInstrument.cpp Source/PluginCatalog.cpp Source/Presets.cpp)
if(APPLE)
    list(APPEND SMARTKEYS_HOST_SOURCES Source/HostingMac.mm)
endif()

#-------------------------------------------------------------------------------
# SmartKeys — un único plugin de instrumento (Channel Rack de FL Studio / pista
# MIDI de Ableton). Fabricante: MIWN. 2.0: no tiene sonido propio; toca el
# instrumento VST3 / Audio Unit cargado en su ranura (es un contenedor).
#   Los códigos PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE NO cambian: de ellos salen
#   los identificadores VST3 y AU, así los proyectos guardados con versiones
#   anteriores siguen encontrando el plugin.
#-------------------------------------------------------------------------------
set(copy_args VST3_COPY_DIR "${SMARTKEYS_VST3_DIR}")
if(APPLE)
    list(APPEND copy_args AU_COPY_DIR "${SMARTKEYS_AU_DIR}")
endif()

juce_add_plugin(SmartKeys
    COMPANY_NAME                  "MIWN"
    COMPANY_COPYRIGHT             "Copyright (c) 2026 MIWN"
    COMPANY_WEBSITE               ""
    BUNDLE_ID                     com.miwn.smartkeys
    PLUGIN_MANUFACTURER_CODE      Jpau
    PLUGIN_CODE                   Smk1
    PRODUCT_NAME                  "SmartKeys"
    DESCRIPTION                   "Smart Chord & Scale Host: Chord Generator, Live Voice Controller and Instrument Slot"
    VERSION                       ${PROJECT_VERSION}
    FORMATS                       VST3 AU Standalone

    IS_SYNTH                      TRUE
    NEEDS_MIDI_INPUT              TRUE
    NEEDS_MIDI_OUTPUT             TRUE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE

    VST3_CATEGORIES               Instrument Synth Generator
    AU_MAIN_TYPE                  kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE               TRUE

    # Carga instrumentos Audio Unit (macOS) dentro de su ranura
    PLUGINHOST_AU                 TRUE

    # NSMicrophoneUsageDescription en el Info.plist de cada formato
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "SmartKeys usa el micrófono para convertir tu voz en notas y acordes MIDI en tiempo real."
    HARDENED_RUNTIME_ENABLED      TRUE
    # La app (Standalone) carga plugins de otros fabricantes en su ranura
    HARDENED_RUNTIME_OPTIONS      com.apple.security.device.audio-input
                                  com.apple.security.cs.disable-library-validation
                                  com.apple.security.cs.allow-unsigned-executable-memory

    COPY_PLUGIN_AFTER_BUILD       ${SMARTKEYS_COPY_AFTER_BUILD}
    ${copy_args})

target_sources(SmartKeys PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SMARTKEYS_MIC_SOURCES}
    ${SMARTKEYS_PORT_SOURCES}
    ${SMARTKEYS_HOST_SOURCES})
target_include_directories(SmartKeys PRIVATE Source)

target_compile_definitions(SmartKeys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1)

target_link_libraries(SmartKeys
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    # CoreAudio (lectura del micrófono) y AVFoundation (permiso de micrófono)
    target_link_libraries(SmartKeys PRIVATE "-framework CoreAudio" "-framework AVFoundation")
endif()

# Instalación local: borrar la versión anterior (y SmartKeys FX de la 1.1–1.4)
# ANTES de copiar la nueva
if(SMARTKEYS_COPY_AFTER_BUILD)
    if(TARGET SmartKeys_VST3)
        add_custom_command(TARGET SmartKeys_VST3 PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMARTKEYS_VST3_DIR}/SmartKeys.vst3" "${SMARTKEYS_VST3_DIR}/SmartKeys FX.vst3"
            COMMENT "SmartKeys: borrando SmartKeys.vst3 / SmartKeys FX.vst3 anteriores de ${SMARTKEYS_VST3_DIR}"
            VERBATIM)
    endif()

    if(APPLE AND TARGET SmartKeys_AU)
        add_custom_command(TARGET SmartKeys_AU PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMARTKEYS_AU_DIR}/SmartKeys.component" "${SMARTKEYS_AU_DIR}/SmartKeys FX.component"
            COMMENT "SmartKeys: borrando SmartKeys.component / SmartKeys FX.component anteriores de ${SMARTKEYS_AU_DIR}"
            VERBATIM)
        add_custom_command(TARGET SmartKeys_AU POST_BUILD
            COMMAND sh -c "killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true"
            COMMENT "SmartKeys: refrescando la caché de Audio Units"
            VERBATIM)
    endif()
endif()

#-------------------------------------------------------------------------------
# Pruebas del motor (cmake -DSMARTKEYS_BUILD_TESTS=ON)
#-------------------------------------------------------------------------------
if(SMARTKEYS_BUILD_TESTS)
    juce_add_console_app(SmartKeysTests PRODUCT_NAME "SmartKeysTests")
    target_sources(SmartKeysTests PRIVATE Tests/EngineTests.cpp ${SMARTKEYS_MIC_SOURCES} ${SMARTKEYS_PORT_SOURCES})
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
    if(APPLE)
        target_link_libraries(SmartKeysTests PRIVATE "-framework CoreAudio" "-framework AVFoundation")
    endif()
endif()

#-------------------------------------------------------------------------------
# Prueba de integración: carga el VST3 compilado como un DAW y comprueba la
# salida MIDI (acordes completos, canal 1, STRUM) y la voz (entrada del plugin
# o micrófono del sistema).
#   cmake -DSMARTKEYS_BUILD_HOST_TESTS=ON ... && cmake --build . --target SmartKeysHostTests
#   SmartKeysHostTests <SmartKeys.vst3> [--stress <segundos>] [--system-mic-only]
#-------------------------------------------------------------------------------
option(SMARTKEYS_BUILD_HOST_TESTS "Compilar la prueba de integración que carga el VST3" OFF)
if(SMARTKEYS_BUILD_HOST_TESTS)
    # Instrumento de prueba que SmartKeys carga en su ranura (VST3; también AU en macOS).
    # Apunta lo que recibe (SMARTKEYS_TESTSYNTH_LOG) para comprobar acordes, STRUM y latencia.
    juce_add_plugin(SmartKeysTestSynth
        COMPANY_NAME                  "MIWN Test"
        BUNDLE_ID                     com.miwn.smartkeys.testsynth
        PLUGIN_MANUFACTURER_CODE      Miwt
        PLUGIN_CODE                   Tsyn
        PRODUCT_NAME                  "SmartKeysTestSynth"
        DESCRIPTION                   "SmartKeys test instrument (not distributed)"
        VERSION                       ${PROJECT_VERSION}
        FORMATS                       VST3 AU
        IS_SYNTH                      TRUE
        NEEDS_MIDI_INPUT              TRUE
        NEEDS_MIDI_OUTPUT             FALSE
        VST3_CATEGORIES               Instrument Synth
        AU_MAIN_TYPE                  kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD       FALSE)
    target_sources(SmartKeysTestSynth PRIVATE Tests/TestSynth.cpp)
    target_compile_definitions(SmartKeysTestSynth PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(SmartKeysTestSynth
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # El catálogo de instrumentos de SmartKeys y la ranura por dentro, por separado (Audio Unit del
    # sistema, búsqueda VST3, bloques más grandes que el preparado, cambios con el audio corriendo)
    juce_add_console_app(SmartKeysCatalogTests PRODUCT_NAME "SmartKeysCatalogTests" PLUGINHOST_AU TRUE)
    target_sources(SmartKeysCatalogTests PRIVATE Tests/CatalogTests.cpp Source/PluginCatalog.cpp Source/Presets.cpp
                                                 Source/HostedInstrument.cpp)
    if(APPLE)
        target_sources(SmartKeysCatalogTests PRIVATE Source/HostingMac.mm)
    endif()
    target_include_directories(SmartKeysCatalogTests PRIVATE Source)
    target_compile_definitions(SmartKeysCatalogTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SmartKeysCatalogTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(SmartKeysHostTests PRODUCT_NAME "SmartKeysHostTests")
    target_sources(SmartKeysHostTests PRIVATE Tests/HostTests.cpp)
    target_compile_definitions(SmartKeysHostTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SmartKeysHostTests
        PRIVATE
            juce::juce_audio_processors_headless
            juce::juce_audio_devices
            juce::juce_events
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    if(APPLE)
        # Como un DAW: declara el uso del micrófono (Info.plist dentro del ejecutable)
        # y no entra en App Nap mientras prueba el audio y el MIDI a tiempo real
        target_sources(SmartKeysHostTests PRIVATE Tests/MacActivity.mm)
        target_include_directories(SmartKeysHostTests PRIVATE Tests)
        target_link_libraries(SmartKeysHostTests PRIVATE "-framework Foundation")
        target_link_options(SmartKeysHostTests PRIVATE
            "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Tests/HostApp-Info.plist")
    endif()
endif()

#-------------------------------------------------------------------------------
# macOS: sonda del micrófono del sistema (prueba en una Mac real / CI). Abre la
# entrada activa con el MISMO código del plugin, comprueba que no cambia la
# frecuencia ni el búfer del dispositivo y detecta la nota que entra.
#   cmake -DSMARTKEYS_BUILD_MIC_PROBE=ON ... && SmartKeysMicProbe [segundos]
#-------------------------------------------------------------------------------
option(SMARTKEYS_BUILD_MIC_PROBE "Compilar la sonda del micrófono del sistema (macOS)" OFF)
if(SMARTKEYS_BUILD_MIC_PROBE AND APPLE)
    juce_add_console_app(SmartKeysMicProbe PRODUCT_NAME "SmartKeysMicProbe")
    target_sources(SmartKeysMicProbe PRIVATE Tests/MicProbe.cpp ${SMARTKEYS_MIC_SOURCES})
    target_include_directories(SmartKeysMicProbe PRIVATE Source)
    target_compile_definitions(SmartKeysMicProbe PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SmartKeysMicProbe
        PRIVATE
            juce::juce_audio_basics
            "-framework CoreAudio" "-framework AVFoundation"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    target_link_options(SmartKeysMicProbe PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Tests/HostApp-Info.plist")
endif()
