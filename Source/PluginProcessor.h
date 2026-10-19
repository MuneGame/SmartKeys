/*
  ==============================================================================

    PluginProcessor.h
    SmartKeys (MIWN) — Piano inteligente, generador de acordes y controlador vocal.

    Un único plugin de instrumento (Channel Rack de FL Studio / pista MIDI de
    Ableton). Siempre suena el piano interno (con su GAIN) y siempre salen los
    acordes completos por la salida MIDI, en el canal 1. Afinación A4 = 440 Hz.

    MIC VOICE (1.5): la voz se lee de la ENTRADA del plugin si el DAW manda
    audio por ella (sidechain); si esa entrada llega en silencio digital, como
    en el Channel Rack de FL Studio, SmartKeys lee él mismo la entrada activa de
    macOS (SystemMicInput: sólo entrada, búfer circular lock-free).

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
#include "FlKeysPianoSynth.h"
#include "MidiOutputRouter.h"
#include "StrumScheduler.h"
#include "SystemMicInput.h"

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

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return JucePlugin_Name; }
    bool acceptsMidi() const override                     { return true; }
    bool producesMidi() const override                    { return true; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.3; }

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

    // Tamaño de la ventana (se guarda con el proyecto)
    float getEditorScale() const noexcept         { return editorScale.load(); }
    void  setEditorScale (float s) noexcept       { editorScale.store (juce::jlimit (0.6f, 2.0f, s)); }

    static juce::StringArray getKeyNames();

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
    void drainScreenNotes (juce::MidiBuffer& out);

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
    smartkeys::FlKeysPianoSynth  pianoSynth;
    smartkeys::MidiOutputRouter  router;
    smartkeys::StrumScheduler    strum;

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

    juce::MidiBuffer routedMidi;                  // acordes completos (antes del STRUM)
    juce::MidiBuffer expandedChordMidi;           // salida final (con STRUM)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoothed;

    static constexpr size_t kMidiReserveBytes = 16384;

    // Muestras del micrófono del sistema que se analizan en cada bloque (fijo)
    std::array<float, (size_t) smartkeys::SystemMicInput::kRingCapacity> systemMicScratch {};

    // Declarado al final: se destruye PRIMERO (el dispositivo se cierra antes
    // que todo lo demás). Además el destructor lo apaga explícitamente.
    smartkeys::SystemMicInput systemMic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessor)
};
