/*
  ==============================================================================

    PluginProcessor.h
    SmartKeys (MIWN) — Generador de acordes, controlador vocal y contenedor de
    instrumentos (2.0, estilo Kontakt / PatchWork).

    Un único plugin de instrumento (Channel Rack de FL Studio / pista MIDI de
    Ableton). SmartKeys convierte cada tecla (o la voz) en el acorde completo y
    se lo toca al instrumento VST3 / Audio Unit cargado en su ranura
    (HostedInstrument); el audio de ese instrumento sale por SmartKeys (con su
    GAIN). Los acordes completos salen también siempre por la salida MIDI, en
    el canal 1. Afinación A4 = 440 Hz. SmartKeys ya no tiene piano propio.

    MIC VOICE (1.5): la voz se lee de la ENTRADA del plugin si el DAW manda
    audio por ella (sidechain); si esa entrada llega en silencio digital, como
    en el Channel Rack de FL Studio, SmartKeys lee él mismo la entrada activa de
    macOS (SystemMicInput: sólo entrada, búfer circular lock-free).

    Teclado MIDI "SmartKeys Chords" (1.6): los mismos acordes completos (con
    STRUM) salen también por un teclado MIDI virtual de macOS. FL Studio graba
    lo que entra por un teclado, no lo que un plugin manda por su salida MIDI:
    así el instrumento enganchado a ese teclado dibuja los acordes en su Piano
    Roll al grabar. Si el DAW devuelve ese teclado al propio SmartKeys, el eco
    se reconoce y se ignora.

    ACORDES AL PIANO ROLL (1.7): FL graba en el Piano Roll de SmartKeys el
    acorde completo que le llega por "SmartKeys Chords", no la tecla. Los
    acordes salen de las teclas de SmartKeys (pantalla y teclado de la Mac con
    su ventana delante) y de la voz, que FL no graba; lo que llega del DAW (el
    Piano Roll al reproducir) suena tal cual: ni se vuelve a convertir en
    acorde ni sale otra vez por el teclado, así que nunca se graba doble.

    El hilo de audio sólo usa memoria fija y atómicos, y nunca llama a la
    interfaz ni al hilo de mensajes.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "ScaleChordEngine.h"
#include "VoicePitchTracker.h"
#include "MidiOutputRouter.h"
#include "StrumScheduler.h"
#include "SystemMicInput.h"
#include "ChordMidiPort.h"
#include "HostedInstrument.h"
#include "Presets.h"

namespace ParamIDs
{
    inline constexpr const char* rootKey     = "rootKey";
    inline constexpr const char* scale       = "scale";
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* chordExt    = "chordExt";
    inline constexpr const char* complexVar  = "complexVar";
    inline constexpr const char* micOn       = "micOn";
    inline constexpr const char* micGate     = "micGate";
    inline constexpr const char* masterGain  = "masterGain";
    inline constexpr const char* strumMs     = "strumMs";
    inline constexpr const char* strumDir    = "strumDir";
}

namespace StrumDir { enum : int { Ascending = 0, Descending = 1 }; }

// De dónde llega la voz que se analiza
namespace VoiceSource { enum : int { None = 0, PluginInput = 1, SystemMic = 2 }; }

// Teclado MIDI virtual "SmartKeys Chords"
namespace ChordPortState { enum : int { Waiting = 0, Active = 1, Unavailable = 2, Unsupported = 3, Off = 4 }; }

//==============================================================================
class SmartKeysAudioProcessor : public juce::AudioProcessor,
                                public juce::VST3ClientExtensions,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
public:
    SmartKeysAudioProcessor();
    ~SmartKeysAudioProcessor() override;

    static constexpr float kReferencePitchHz = 440.0f;   // afinación fija (A4)
    static constexpr int   kMidiChannel = 1;             // salida MIDI fija en el canal 1

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // En bypass no suena nada y no queda ninguna nota encendida (instrumento, MIDI ni teclado virtual)
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

    // Exportar (FL: render): el instrumento cargado también trabaja fuera de tiempo real
    void setNonRealtime (bool isNonRealtime) noexcept override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return JucePlugin_Name; }
    bool acceptsMidi() const override                     { return true; }
    bool producesMidi() const override                    { return true; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return hosted.getTailLengthSeconds(); }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // VST3: instrumento; la entrada de audio es auxiliar (sidechain, como un vocoder)
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override                     { return false; }

    //==========================================================================
    // API para la interfaz (hilo de mensajes). Todo son atómicos o FIFOs
    // lock-free: la interfaz lee, el hilo de audio escribe, nadie espera.
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    uint16_t getCustomMask() const noexcept     { return customMask.load(); }
    void toggleCustomNote (int pitchClass) noexcept;

    // Teclado en pantalla / teclado de la PC -> FIFO lock-free -> hilo de audio
    void pushScreenNote (int note, int velocity, bool isNoteOn) noexcept;

    // Suelta todo lo que tocan el teclado en pantalla y el de la PC (al elegir otra KEY):
    // ninguna nota de la interfaz queda colgada. Lo que llega del DAW no se toca.
    void releaseScreenNotes() noexcept              { releaseScreenRequested.store (true, std::memory_order_release); }

    // Notas de salida que suenan de verdad (con STRUM se encienden una a una)
    uint64_t getActiveNotesLo() const noexcept  { return activeNotesBitmask[0].load (std::memory_order_acquire); }
    uint64_t getActiveNotesHi() const noexcept  { return activeNotesBitmask[1].load (std::memory_order_acquire); }
    uint64_t getLastEventPacked() const noexcept      { return router.lastEventPacked.load (std::memory_order_acquire); }
    uint64_t getLastVoiceEventPacked() const noexcept { return router.lastVoiceEventPacked.load (std::memory_order_acquire); }

    // Micrófono: detector, nivel, fuente y estado del micrófono del sistema
    const smartkeys::VoicePitchTracker& getVoiceTracker() const noexcept { return voiceTracker; }
    float getMicLevelDb() const noexcept          { return micLevelDb.load(); }
    bool  isMicSourceAvailable() const noexcept   { return voiceSource.load() != VoiceSource::None; }
    int   getVoiceSource() const noexcept         { return voiceSource.load(); }
    bool  isVoiceChordSounding() const noexcept   { return voiceChordSounding.load(); }

    smartkeys::MicDeviceState getSystemMicState() const noexcept { return systemMic.getState(); }
    juce::String getSystemMicDeviceName() const                  { return systemMic.getDeviceName(); }    // hilo de mensajes
    bool isSystemMicReplacingBluetooth() const noexcept          { return systemMic.isUsingBuiltInInsteadOfBluetooth(); }
    double getSystemMicSampleRate() const noexcept               { return systemMic.getSampleRate(); }

    double getStrumMs() const noexcept            { return pStrumMs->load(); }

    // Teclado MIDI virtual "SmartKeys Chords"
    int getChordPortState() const noexcept        { return chordPortState.load(); }
    static juce::String getChordPortName()        { return smartkeys::ChordMidiPort::getDeviceName(); }
    bool isChordPortLooping() const noexcept      { return chordPortLooping.load (std::memory_order_relaxed); }
    // El DAW avisa a SmartKeys de que está grabando: lo que FL repite en cada vuelta no sale otra vez
    bool isHostRecording() const noexcept         { return hostRecording.load (std::memory_order_relaxed); }
    void markInUse() noexcept                     { pluginInUse.store (true); }   // la ventana se abrió

    // Encendido / apagado (hilo de mensajes; se guarda con el proyecto). Por
    // defecto encendido en FL Studio y en la app; apagado en los DAW que meten
    // cualquier teclado nuevo en la grabación de la pista (Logic, Ableton...).
    bool isChordPortEnabled() const noexcept      { return chordPortEnabled.load(); }
    void setChordPortEnabled (bool shouldBeEnabled);
    static bool isChordPortEnabledByDefault (juce::AudioProcessor::WrapperType);

    // "ACORDES AL PIANO ROLL" (hilo de mensajes; se guarda con el proyecto; enciende el teclado)
    bool isPianoRollChords() const noexcept       { return pianoRollChords.load(); }
    void setPianoRollChords (bool shouldBeOn);
    // En la app no hay Piano Roll (SMARTKEYS_TEST_PIANO_ROLL=1 lo permite para las capturas de prueba)
    bool canUsePianoRollChords() const noexcept   { return wrapperType != wrapperType_Standalone || testPianoRollInApp; }
    // Lo que sale por el teclado no vuelve: FL no lo está recibiendo (en ese modo debería)
    bool isChordPortNotReturning() const noexcept { return chordPortNoLoop.load (std::memory_order_relaxed); }
    // Cuenta las notas que llegan del DAW con el transporte parado en ese modo (se toca con el teclado de FL)
    uint32_t getDawLiveNoteCount() const noexcept { return dawLiveNotes.load (std::memory_order_relaxed); }

    // Tamaño de la ventana (se guarda con el proyecto)
    float getEditorScale() const noexcept         { return editorScale.load(); }
    void  setEditorScale (float s) noexcept       { editorScale.store (juce::jlimit (0.6f, 2.0f, s)); }

    // La vista de la ventana: SMARTKEYS (false) o SYNTH (la interfaz del instrumento cargado,
    // dentro de la ventana). Se guarda con el proyecto.
    bool isSynthViewWanted() const noexcept       { return synthViewWanted.load(); }
    void setSynthViewWanted (bool b) noexcept     { synthViewWanted.store (b); }

    static juce::StringArray getKeyNames();

    // Ranura de instrumento (la interfaz la usa desde el hilo de mensajes)
    smartkeys::HostedInstrument& getHostedInstrument() noexcept   { return hosted; }

    // Presets propios de SmartKeys (hilo de mensajes). El GAIN no forma parte del preset.
    std::unique_ptr<juce::XmlElement> createPresetXml (bool includeInstrument);
    void applyPreset (const juce::XmlElement& preset, const juce::String& name);
    juce::String getPresetName() const;
    void setPresetName (const juce::String&);

    // La entrada del plugin se considera "conectada" con cualquier muestra por
    // encima de -120 dBFS; el silencio digital (FL sin sidechain) no cuenta.
    static constexpr float  kInputSignalFloor = 1.0e-6f;
    static constexpr double kInputSilenceSeconds = 0.5;    // silencio para pasar al micrófono del sistema
    static constexpr double kInputHoldMs = 3000.0;         // el micrófono del sistema se cierra si el DAW manda voz

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void updateEngineIfNeeded();
    void drainScreenNotes();

    // Cada tecla va a los dos routers (salida MIDI + instrumento, y teclado virtual)
    void routeNoteOn (smartkeys::MidiOutputRouter::Source source, int trigger, juce::uint8 velocity, int pos, bool toPort);
    void routeNoteOff (smartkeys::MidiOutputRouter::Source source, int trigger, int pos);
    void routeReleaseAll (int pos);

    // Grabación en bucle: ¿esta tecla ya salió por el teclado en esta misma
    // posición musical en una vuelta anterior? (FL está repitiendo lo que grabó)
    bool isLoopRepeat (int note, double ppq) noexcept;
    bool matchesEarlierPass (int note, double ppq, int pass) const noexcept;
    void rememberLoopTrigger (int note, double ppq, int pass) noexcept;

    // ACORDES AL PIANO ROLL: notas del DAW que suenan tal cual (instrumento + salida MIDI)
    void playPlainNoteOn (int note, int velocity, int pos) noexcept;
    void playPlainNoteOff (int note, int pos) noexcept;
    void releasePlainNotes (int pos, juce::MidiBuffer& out) noexcept;

    // UNA TECLA, UN ACORDE (sólo hilo de audio). Cada tecla disparadora recuerda desde dónde
    // está pulsada (el DAW / un teclado MIDI, o la pantalla / el teclado de la PC). Un NoteOn
    // repetido sin su NoteOff, o la misma tecla pulsada a la vez en el DAW y en la pantalla, no
    // dispara un segundo acorde; un NoteOff de una tecla que no estaba pulsada no hace nada.
    static constexpr uint8_t kHeldMidi = 1, kHeldScreen = 2;
    bool pressTrigger (uint8_t sourceBit, int note) noexcept;     // true: disparar el acorde
    bool releaseTrigger (uint8_t sourceBit, int note) noexcept;   // true: soltarlo (nadie más la pulsa)
    void releaseHeldScreenTriggers (int pos);

    void updateChordPort();
    void disconnectChordPort();
    double echoWindowMs (double nowMs) const noexcept;
    double echoEarlyMs() const noexcept;
    bool isPortEchoCandidate (int note, double arrivalMs) const noexcept;
    void releaseSuspectTriggers() noexcept;
    void updateEchoEvidence (double nowMs) noexcept;
    bool isPortEchoNoteOn (int note, double arrivalMs) noexcept;
    bool isPortEchoNoteOff (int note, double arrivalMs) noexcept;
    void feedChordPort (const juce::MidiBuffer& out, int numSamples, double blockStartMs) noexcept;
    void resetEchoState() noexcept;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pRootKey     = nullptr;
    std::atomic<float>* pScale       = nullptr;
    std::atomic<float>* pMode        = nullptr;
    std::atomic<float>* pChordExt    = nullptr;
    std::atomic<float>* pComplexVar  = nullptr;
    std::atomic<float>* pMicOn       = nullptr;
    std::atomic<float>* pMicGate     = nullptr;
    std::atomic<float>* pMasterGain  = nullptr;
    std::atomic<float>* pStrumMs     = nullptr;
    std::atomic<float>* pStrumDir    = nullptr;

    smartkeys::ScaleChordEngine  engine;
    smartkeys::VoicePitchTracker voiceTracker;      // voz: entrada del plugin o micrófono del sistema
    smartkeys::MidiOutputRouter  router;
    smartkeys::StrumScheduler    strum;

    // Teclado "SmartKeys Chords": su propio router y STRUM, con sólo las teclas que
    // deben salir por él (al grabar en bucle, lo que FL repite de una vuelta anterior no sale)
    smartkeys::MidiOutputRouter  portRouter;
    smartkeys::StrumScheduler    portStrum;

    // Personalización de las 12 notas (bit 0 = C)
    std::atomic<uint16_t> customMask { 0b101010110101 };

    // Caché del estado aplicado al motor (sólo hilo de audio)
    int      appliedRoot = -1, appliedScale = -1, appliedMode = -1, appliedExt = -1, appliedVar = -1;
    uint16_t appliedMask = 0;
    bool     micWasOn = false;
    double   currentSampleRate = 44100.0;

    // Fuente de la voz (sólo hilo de audio)
    int      trackerSource = -1;
    double   trackerRate = 0.0;
    uint32_t trackerGeneration = 0;
    int      inputSilentSamples = 1 << 30;

    // Estado del micrófono para la interfaz y el temporizador
    std::atomic<float> micLevelDb { -100.0f };
    std::atomic<int>   voiceSource { VoiceSource::None };
    std::atomic<bool>  pluginInputActive { false };
    std::atomic<bool>  voiceChordSounding { false };
    double lastPluginInputActiveMs = -1.0e9;          // sólo hilo de mensajes

    // FIFO del teclado en pantalla / teclado de la PC (tamaño fijo, lock-free)
    struct ScreenNoteEvent { int note = 60; int velocity = 100; bool on = true; };
    static constexpr int kScreenFifoSize = 256;
    juce::AbstractFifo screenFifo { kScreenFifoSize };
    std::array<ScreenNoteEvent, kScreenFifoSize> screenEvents {};

    std::array<std::atomic<uint64_t>, 2> activeNotesBitmask {};
    std::atomic<float> editorScale { 1.0f };
    std::atomic<bool> synthViewWanted { false };

    juce::MidiBuffer routedMidi;                  // acordes completos (antes del STRUM)
    juce::MidiBuffer expandedChordMidi;           // salida final (con STRUM)
    juce::MidiBuffer portRoutedMidi, portExpandedMidi;   // lo mismo para el teclado virtual
    juce::MidiBuffer plainMidi;                   // notas del DAW tal cual (ACORDES AL PIANO ROLL)
    const juce::MidiBuffer silentMidi;            // bypass: el instrumento sólo suelta sus notas
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoothed;

    static constexpr size_t kMidiReserveBytes = 16384;

    // Muestras del micrófono del sistema que se analizan en cada bloque (fijo)
    std::array<float, (size_t) smartkeys::SystemMicInput::kRingCapacity> systemMicScratch {};

    // Teclado MIDI virtual "SmartKeys Chords" (se conecta en el hilo de mensajes)
    smartkeys::ChordPortFeed portFeed;
    std::unique_ptr<juce::SharedResourcePointer<smartkeys::ChordMidiPort>> chordPort;
    std::atomic<bool> portFeedActive { false }, pluginInUse { false }, chordPortEnabled { true };
    std::atomic<int>  chordPortState { ChordPortState::Waiting };
    int  portRetryTicks = 0;
    bool processedAnyBlock = false, bypassActive = false;

    // Eco del teclado virtual (sólo hilo de audio). Ver isPortEchoNoteOn().
    std::array<uint32_t, 128> echoOnMatched {}, echoOnAccounted {}, echoOffMatched {};
    std::array<uint8_t, 128> echoSuppressed {};
    std::array<bool, 128> midiKeyDown {}, suspectTrigger {};
    int    echoedStreak = 0, unechoedStreak = 0;
    bool   echoGroupInBlock = false;
    double loopConfirmedUntilMs = -1.0e12, noLoopUntilMs = -1.0e12;
    double maxBlockMs = 12.0;
    double portClockMs = 0.0;            // hora (reloj de pared) del inicio del bloque, por muestras
    bool   portClockValid = false;

    // Grabación en bucle (sólo hilo de audio): dónde (en negras) salió cada tecla
    // por el teclado en esta toma, y en qué vuelta del bucle
    struct LoopTrigger { double ppq = 0.0; int pass = 0; };
    static constexpr int kLoopTriggersPerNote = 64;
    std::array<std::array<LoopTrigger, (size_t) kLoopTriggersPerNote>, 128> loopTriggers {};
    std::array<uint8_t, 128> loopTriggerCount {}, loopTriggerNext {};
    bool   recordingTake = false;
    int    loopPass = 0;
    double lastBlockStartPpq = 0.0, lastBlockEndPpq = 0.0;
    // Final del bucle: si el DAW no corta el bloque justo ahí, la primera nota de la
    // vuelta siguiente llega en un bloque que empezó antes del final (con una posición
    // "de más"). Se aprende en cada vuelta (o lo dice el DAW con sus puntos de bucle).
    double loopLengthBeats = 0.0;                 // 0: todavía no se sabe
    double loopSeamPpq = 0.0, loopSeamMaxPpq = 0.0;   // el final está en [loopSeamPpq, loopSeamMaxPpq]
    static constexpr double kRepeatToleranceBeats = 0.25;   // una semicorchea (cubre la cuantización al grabar)
    std::atomic<bool> chordPortLooping { false };
    std::atomic<bool> hostRecording { false };   // el DAW avisa de que graba (la ventana muestra "· REC")

    // ACORDES AL PIANO ROLL
    std::atomic<bool> pianoRollChords { false };
    std::atomic<bool> chordPortNoLoop { false };
    std::atomic<uint32_t> dawLiveNotes { 0 };
    bool appliedPianoRollChords = false;          // sólo hilo de audio: al cambiar de modo se suelta todo
    const bool testPianoRollInApp = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TEST_PIANO_ROLL", {}) == "1";
    std::array<bool, 128> plainOn {};             // notas del DAW que suenan tal cual (una vez cada una)

    // Una tecla, un acorde
    std::array<uint8_t, 128> triggerHeld {};      // kHeldMidi | kHeldScreen
    std::atomic<bool> releaseScreenRequested { false };

    static constexpr double kLoopEchoWindowMs = 600.0;   // con el bucle confirmado
    static constexpr double kLoopMemoryMs = 10000.0;     // el bucle se da por confirmado 10 s (cada eco lo renueva)
    static constexpr double kNoLoopMemoryMs = 60000.0;   // "sin bucle" vale 60 s

    // Preset cargado o guardado por última vez (se guarda con el proyecto)
    juce::CriticalSection presetNameLock;
    juce::String presetName;

    // El instrumento cargado. El destructor lo cierra explícitamente (ventana,
    // audio y plugin, en ese orden) antes que nada.
    smartkeys::HostedInstrument hosted;

    // Declarado al final: se destruye PRIMERO (el dispositivo se cierra antes
    // que todo lo demás). Además el destructor lo apaga explícitamente.
    smartkeys::SystemMicInput systemMic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessor)
};
