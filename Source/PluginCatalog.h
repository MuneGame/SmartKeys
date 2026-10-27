/*
  ==============================================================================

    PluginCatalog.h
    SmartKeys (MIWN) — Catálogo de instrumentos que se pueden cargar en la
    ranura de SmartKeys (2.0: SmartKeys es un contenedor de plugins).

    Uno por proceso, compartido por todas las instancias de SmartKeys
    (juce::SharedResourcePointer). Todo se usa desde el hilo de mensajes.

      - Audio Unit (macOS): la lista de instrumentos registrados en el sistema,
        al instante y SIN abrir ningún plugin (AudioComponentFindNext).
      - VST3: se buscan en las carpetas estándar (y en VST3_PATH). La búsqueda
        va un archivo por tic del temporizador; muchos VST3 traen su
        moduleinfo.json y se describen sin cargarlos. Si un plugin cerrara el
        programa al examinarlo, la próxima búsqueda lo salta ("dead man's
        pedal"). El resultado se guarda en disco.
      - SÓLO GENERADORES DE SONIDO: la búsqueda apunta únicamente los plugins
        que se declaran instrumento (isInstrument) y suenan por sí mismos. Un
        archivo que sólo trae efectos (EQ, reverb, compresor…) se aparta y no se
        vuelve a abrir en las búsquedas de "nuevos". Tampoco se ofrecen, aunque
        se declaren instrumento: los anfitriones de plugins (AudioGridder,
        Element, PatchWork, Unify, Carla…), patchers, navegadores de samples y
        los que dicen en su nombre o categoría efecto, analizador, ruteo, MIDI,
        utilidad o control; ni los que no tienen salida de audio (si al
        cargarlo resulta que no tiene, sale de la lista); ni el propio SmartKeys.
      - Carga protegida: si el programa se cerró mientras se creaba un plugin,
        al abrir otra vez el proyecto ese plugin no se carga solo (se avisa y
        se puede reintentar a mano). Así un plugin roto no deja el proyecto
        imposible de abrir.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace smartkeys
{

class PluginCatalog : private juce::Timer
{
public:
    PluginCatalog();
    ~PluginCatalog() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept   { return formats; }

    // Instrumentos que se pueden cargar, ordenados por formato, fabricante y nombre
    juce::Array<juce::PluginDescription> getInstruments();

    // Búsqueda de instrumentos VST3 (avanza sola, un archivo por tic)
    void startVst3Scan (bool rescanEverything);
    bool isScanning() const noexcept                { return scanner != nullptr; }
    float getScanProgress() const noexcept;
    juce::String getScanningName() const            { return scanningName; }
    bool hasEverScannedVst3() const noexcept        { return everScanned; }
    int getLastScanNewInstruments() const noexcept  { return lastScanNew; }
    bool hasVst3Format() const noexcept             { return findFormat ("VST3") != nullptr; }

    // La interfaz vuelve a pintar cuando cambia (búsqueda, lista)
    uint32_t getChangeCounter() const noexcept      { return changeCounter.load (std::memory_order_acquire); }

    // Carga protegida
    void markLoadStarted (const juce::PluginDescription&);
    void markLoadFinished (const juce::PluginDescription&);
    bool crashedDuringLastLoad (const juce::PluginDescription&) const;
    void forgetCrash (const juce::PluginDescription&);

    // ~/Library/Application Support/MIWN/SmartKeys (Mac), %APPDATA%\MIWN\SmartKeys (Windows).
    // SMARTKEYS_USER_DATA la cambia (pruebas automáticas).
    static juce::File getUserDataFolder();

    // SmartKeys no se carga dentro de sí mismo
    static bool isSmartKeysItself (const juce::PluginDescription&);

    // ¿Se puede cargar en la ranura? Un instrumento (isInstrument) que no sea SmartKeys, ni un
    // anfitrión de plugins / patcher / navegador de samples, cuyo nombre y categoría no digan
    // efecto, analizador, ruteo, MIDI, utilidad o control y, si se saben sus canales, con
    // salida de audio
    static bool isLoadableInstrument (const juce::PluginDescription&);
    static bool isHostOrBrowser (const juce::PluginDescription&);       // nombre / fabricante
    static bool hasNonGeneratorName (const juce::PluginDescription&);   // nombre / categoría

    // Al cargarlo no tenía salida de audio: ya no se ofrece (se guarda; "Volver a buscar
    // todos" le da otra oportunidad)
    void excludeNonGenerator (const juce::PluginDescription&);
    bool isExcluded (const juce::PluginDescription&) const;

    // "Audio Unit" / "VST3"
    static juce::String getFormatDisplayName (const juce::PluginDescription&);

private:
    void timerCallback() override;
    void finishScan();
    void saveVst3List();
    juce::AudioPluginFormat* findFormat (const juce::String& name) const;
    int countVst3Instruments() const;
    bool pruneNonInstruments();   // la lista guardada por una versión anterior podía traer efectos
    static juce::String loadKey (const juce::PluginDescription&);
    void writeLoadMarkers();
    void saveExcluded();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList vst3List;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    juce::String scanningName;
    bool scanEverything = false, everScanned = false;
    int instrumentsBeforeScan = 0, lastScanNew = 0;
    std::atomic<uint32_t> changeCounter { 0 };

    juce::StringArray loadsInProgress;      // cargas en curso en este proceso
    juce::StringArray crashedLastTime;      // cargas que no terminaron la última vez
    juce::StringArray excluded;             // sin salida de audio al cargarlos

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};

#if JUCE_MAC
// Instrumentos Audio Unit registrados (kAudioUnitType_MusicDevice), sin instanciarlos
juce::Array<juce::PluginDescription> listAudioUnitInstruments();
#endif

} // namespace smartkeys
