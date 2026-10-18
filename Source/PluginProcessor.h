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
    Ambas pueden además leer el micrófono DIRECTAMENTE desde la interfaz de
    audio (selector "DEVICE"), sin depender del ruteo del DAW.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "ScaleChordEngine.h"
#include "VoicePitchTracker.h"
#include "FlKeysPianoSynth.h"
#include "MidiOutputRouter.h"
#include "HardwareMicInput.h"
#include "ChordRecorder.h"

#ifndef SMARTKEYS_FX_VARIANT
 #define SMARTKEYS_FX_VARIANT 0
#endif

namespace ParamIDs
{
    inline constexpr const char* refPitch    = "refPitch";
    inline constexpr const char* rootKey     = "rootKey";
    inline constexpr const char* scale       = "scale";
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* chordExt    = "chordExt";
    inline constexpr const char* complexVar  = "complexVar";
    inline constexpr const char* micOn       = "micOn";
    inline constexpr const char* micSource   = "micSource";
    inline constexpr const char* micGate     = "micGate";
    inline constexpr const char* outputMode  = "outputMode";
    inline constexpr const char* midiOutCh   = "midiOutCh";
    inline constexpr const char* masterGain  = "masterGain";
    inline constexpr const char* microTuning = "microTuning";
}

namespace OutputMode { enum : int { InternalPiano = 0, ExternalMidi = 1, Both = 2 }; }
namespace MicSource  { enum : int { DawInput = 0, HardwareDevice = 1 }; }

//==============================================================================
class SmartKeysAudioProcessor : public juce::AudioProcessor,
                                public juce::VST3ClientExtensions,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
public:
    SmartKeysAudioProcessor();
    ~SmartKeysAudioProcessor() override;

    static constexpr bool isFxVariant = SMARTKEYS_FX_VARIANT != 0;

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
    // API para la interfaz (hilo de mensajes)
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    uint16_t getCustomMask() const noexcept     { return customMask.load(); }
    void toggleCustomNote (int pitchClass) noexcept;

    // Teclado en pantalla / teclado de la PC -> FIFO lock-free -> hilo de audio
    void pushScreenNote (int note, int velocity, bool isNoteOn) noexcept;

    void requestPanic() noexcept                { panicRequested.store (true); }

    // Notas de salida que suenan (bitmask atómico de 128 bits)
    uint64_t getActiveNotesLo() const noexcept  { return activeNotesBitmask[0].load (std::memory_order_acquire); }
    uint64_t getActiveNotesHi() const noexcept  { return activeNotesBitmask[1].load (std::memory_order_acquire); }
    uint64_t getLastEventPacked() const noexcept      { return router.lastEventPacked.load (std::memory_order_acquire); }
    uint64_t getLastVoiceEventPacked() const noexcept { return router.lastVoiceEventPacked.load (std::memory_order_acquire); }

    // Micrófono: detector de la fuente activa, nivel y estado
    const smartkeys::VoicePitchTracker& getActiveVoiceTracker() const noexcept
    {
        return activeVoiceSource.load() == MicSource::HardwareDevice ? hardwareTracker : dawTracker;
    }
    float getMicLevelDb() const noexcept          { return micLevelDb.load(); }
    bool  isMicSourceAvailable() const noexcept   { return micSourceAvailable.load(); }
    bool  isVoiceChordSounding() const noexcept   { return voiceChordSounding.load(); }

    // Micrófono de hardware (vía B) — hilo de mensajes
    juce::StringArray getHardwareInputDevices()   { return hardwareMic.getInputDeviceNames(); }
    juce::String getDefaultHardwareInputDevice()  { return hardwareMic.getDefaultInputDeviceName(); }
    juce::String getHardwareDeviceName() const    { const juce::ScopedLock sl (hardwareStateLock); return hardwareDeviceName; }
    int  getHardwareChannelMode() const noexcept  { return hardwareChannelMode.load(); }
    bool isHardwareMicOpen() const noexcept       { return hardwareMic.isOpen(); }
    juce::String getHardwareStatus() const        { const juce::ScopedLock sl (hardwareStateLock); return hardwareStatus; }
    double getHardwareMsSinceSignal() const noexcept { return hardwareMic.getMillisecondsSinceSignal(); }
    double getHardwareMsSinceStart() const noexcept  { return hardwareMic.getMillisecondsSinceStart(); }
    void setHardwareInput (const juce::String& deviceName, int channelMode, bool activate);
    void retryHardwareInput();

    // Grabador de acordes (DRAG CHORDS TO PIANO ROLL) — hilo de mensajes
    int  getNumCapturedChords() const noexcept    { return recorder.getNumChords(); }
    bool isCaptureFull() const noexcept           { return recorder.isFull(); }
    void clearCapturedChords();
    juce::File exportCapturedChords (const juce::String& baseName);
    bool getExportQuantize() const noexcept       { return exportQuantize.load(); }
    void setExportQuantize (bool q) noexcept      { exportQuantize.store (q); }
    double getHostBpm() const noexcept            { return hostBpm.load(); }

    static juce::StringArray getKeyNames();

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void updateEngineIfNeeded();
    void drainScreenNotes (juce::MidiBuffer& out, int outChannel);
    void captureOutputNotes (const juce::MidiBuffer& notes, double sampleRate, double bpm, int numSamples);
    void openHardwareMicIfNeeded();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pRefPitch    = nullptr;
    std::atomic<float>* pRootKey     = nullptr;
    std::atomic<float>* pScale       = nullptr;
    std::atomic<float>* pMode        = nullptr;
    std::atomic<float>* pChordExt    = nullptr;
    std::atomic<float>* pComplexVar  = nullptr;
    std::atomic<float>* pMicOn       = nullptr;
    std::atomic<float>* pMicSource   = nullptr;
    std::atomic<float>* pMicGate     = nullptr;
    std::atomic<float>* pOutputMode  = nullptr;
    std::atomic<float>* pMidiOutCh   = nullptr;
    std::atomic<float>* pMasterGain  = nullptr;
    std::atomic<float>* pMicroTuning = nullptr;

    smartkeys::ScaleChordEngine  engine;
    smartkeys::VoicePitchTracker dawTracker;        // vía A: bus de entrada del DAW
    smartkeys::VoicePitchTracker hardwareTracker;   // vía B: micrófono directo
    smartkeys::FlKeysPianoSynth  pianoSynth;
    smartkeys::MidiOutputRouter  router;
    smartkeys::HardwareMicInput  hardwareMic;

    // Personalización de las 12 notas (bit 0 = C)
    std::atomic<uint16_t> customMask { 0b101010110101 };

    // Caché del estado aplicado al motor (sólo hilo de audio)
    int      appliedRoot = -1, appliedScale = -1, appliedMode = -1, appliedExt = -1, appliedVar = -1;
    uint16_t appliedMask = 0;
    float    appliedRef = 0.0f;
    bool     appliedMicro = false;
    bool     micWasOn = false;
    bool     pianoWasActive = true;
    int      lastVoiceSource = -1;
    double   currentSampleRate = 44100.0;

    // Estado del micrófono para la interfaz
    std::atomic<int>   activeVoiceSource { MicSource::DawInput };
    std::atomic<float> micLevelDb { -100.0f };
    std::atomic<bool>  micSourceAvailable { false };
    std::atomic<bool>  voiceChordSounding { false };

    // Micrófono de hardware. Lo usan el hilo de mensajes y setStateInformation
    // (que algunos hosts llaman desde otro hilo): protegido por un candado que
    // el hilo de audio nunca toma.
    juce::CriticalSection hardwareStateLock;
    juce::String     hardwareDeviceName;
    juce::String     hardwareStatus;
    bool             hardwareOpenFailed = false;
    std::atomic<int>  hardwareChannelMode { (int) smartkeys::HardwareMicInput::ChannelMode::Mix };
    std::atomic<bool> hardwareReopenRequested { false };
    std::vector<float> hardwareScratch;   // reservado en prepareToPlay

    // FIFO del teclado en pantalla
    struct ScreenNoteEvent { int note = 60; int velocity = 100; bool on = true; };
    static constexpr int kScreenFifoSize = 256;
    juce::AbstractFifo screenFifo { kScreenFifoSize };
    std::array<ScreenNoteEvent, kScreenFifoSize> screenEvents {};

    std::atomic<bool> panicRequested { false };
    std::array<std::atomic<uint64_t>, 2> activeNotesBitmask {};

    // Grabador de acordes
    smartkeys::CaptureFifo   captureFifo;
    smartkeys::ChordRecorder recorder;
    double captureClockQn = 0.0;                  // reloj musical (hilo de audio)
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool>   exportQuantize { true };

    juce::MidiBuffer expandedChordMidi;           // acordes completos de salida
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoothed;

    static constexpr size_t kMidiReserveBytes = 16384;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessor)
};
