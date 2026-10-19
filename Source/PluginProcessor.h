/*
  ==============================================================================

    PluginProcessor.h
    SmartKeys — Piano inteligente, generador de acordes y controlador vocal.

    Se compila en dos variantes desde el mismo código:
      - SmartKeys     (instrumento: Channel Rack de FL / pista MIDI de Ableton).
                      Entrada de micrófono como sidechain (como un vocoder).
      - SmartKeys FX  (efecto, SMARTKEYS_FX_VARIANT=1): se inserta en la pista
                      del micrófono del Mixer y recibe la voz como entrada
                      principal, sin ruteos.
    La voz llega SÓLO por el bus de entrada del plugin: el plugin nunca abre
    dispositivos de audio propios dentro del DAW.

    1.4: versión simplificada. Siempre suena el piano interno (con su GAIN) y
    siempre salen los acordes completos por la salida MIDI; afinación fija en
    A4 = 440 Hz. Sin progresiones, sin exportación .mid y sin puerto MIDI
    virtual. El hilo de audio sólo usa memoria fija y atómicos, y nunca llama
    a la interfaz ni al hilo de mensajes.

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

#ifndef SMARTKEYS_FX_VARIANT
 #define SMARTKEYS_FX_VARIANT 0
#endif

namespace ParamIDs
{
    inline constexpr const char* rootKey     = "rootKey";
    inline constexpr const char* scale       = "scale";
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* chordExt    = "chordExt";
    inline constexpr const char* complexVar  = "complexVar";
    inline constexpr const char* micOn       = "micOn";
    inline constexpr const char* micGate     = "micGate";
    inline constexpr const char* midiOutCh   = "midiOutCh";
    inline constexpr const char* masterGain  = "masterGain";
    inline constexpr const char* strumMs     = "strumMs";
    inline constexpr const char* strumDir    = "strumDir";
}

namespace StrumDir { enum : int { Ascending = 0, Descending = 1 }; }

//==============================================================================
class SmartKeysAudioProcessor : public juce::AudioProcessor,
                                public juce::VST3ClientExtensions,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    SmartKeysAudioProcessor();
    ~SmartKeysAudioProcessor() override;

    static constexpr bool isFxVariant = SMARTKEYS_FX_VARIANT != 0;
    static constexpr float kReferencePitchHz = 440.0f;   // afinación fija (A4)

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
    // VST3: en el instrumento la entrada es auxiliar (sidechain, como un vocoder);
    // en SmartKeys FX es la entrada principal de la pista donde se inserta.
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override                     { return isFxVariant; }

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

    // Micrófono (bus de entrada del plugin): detector, nivel y estado
    const smartkeys::VoicePitchTracker& getVoiceTracker() const noexcept { return voiceTracker; }
    float getMicLevelDb() const noexcept          { return micLevelDb.load(); }
    bool  isMicSourceAvailable() const noexcept   { return micSourceAvailable.load(); }
    bool  isVoiceChordSounding() const noexcept   { return voiceChordSounding.load(); }

    double getStrumMs() const noexcept            { return pStrumMs->load(); }

    // Tamaño de la ventana (se guarda con el proyecto)
    float getEditorScale() const noexcept         { return editorScale.load(); }
    void  setEditorScale (float s) noexcept       { editorScale.store (juce::jlimit (0.6f, 2.0f, s)); }

    static juce::StringArray getKeyNames();

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateEngineIfNeeded();
    void drainScreenNotes (juce::MidiBuffer& out, int outChannel);

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pRootKey     = nullptr;
    std::atomic<float>* pScale       = nullptr;
    std::atomic<float>* pMode        = nullptr;
    std::atomic<float>* pChordExt    = nullptr;
    std::atomic<float>* pComplexVar  = nullptr;
    std::atomic<float>* pMicOn       = nullptr;
    std::atomic<float>* pMicGate     = nullptr;
    std::atomic<float>* pMidiOutCh   = nullptr;
    std::atomic<float>* pMasterGain  = nullptr;
    std::atomic<float>* pStrumMs     = nullptr;
    std::atomic<float>* pStrumDir    = nullptr;

    smartkeys::ScaleChordEngine  engine;
    smartkeys::VoicePitchTracker voiceTracker;      // voz: bus de entrada del plugin
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

    // Estado del micrófono para la interfaz
    std::atomic<float> micLevelDb { -100.0f };
    std::atomic<bool>  micSourceAvailable { false };
    std::atomic<bool>  voiceChordSounding { false };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessor)
};
