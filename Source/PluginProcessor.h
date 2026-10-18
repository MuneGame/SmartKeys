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

    1.2: la salida MIDI de los acordes completos está SIEMPRE activa; STRUM
    (entrada progresiva ASC/DESC); captura inteligente sincronizada con el
    transporte del DAW; carril de progresión + paleta de acordes estilo Klimper.

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
#include "StrumScheduler.h"
#include "Progression.h"

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
    inline constexpr const char* strumMs     = "strumMs";
    inline constexpr const char* strumDir    = "strumDir";
}

// La salida MIDI de los acordes está siempre activa; sólo se elige si suena el piano interno
namespace OutputMode  { enum : int { PianoAndMidi = 0, MidiOnly = 1 }; }
namespace MicSource   { enum : int { DawInput = 0, HardwareDevice = 1 }; }
namespace StrumDir    { enum : int { Ascending = 0, Descending = 1 }; }
namespace CaptureSync { enum : int { PlayOrRecord = 0, RecordOnly = 1, ManualOnly = 2 }; }
namespace TakeMode    { enum : int { Replace = 0, Add = 1 }; }

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

    // Notas de salida que suenan de verdad (con STRUM se encienden una a una)
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

    //==========================================================================
    // Progresión (DRAG CHORDS TO PIANO ROLL) — hilo de mensajes.
    // El modelo se protege con un candado (setStateInformation puede llegar
    // desde otro hilo); el hilo de audio nunca lo toca: usa una instantánea.
    template <typename Fn>
    auto withProgression (Fn&& fn)
    {
        const juce::ScopedLock sl (progressionLock);
        return fn (progression);
    }
    void progressionEdited();                                   // tras editar: instantánea + versión
    uint32_t getProgressionVersion() const noexcept   { return progressionVersion.load(); }
    int  getNumProgressionChords() const noexcept     { return numProgressionChords.load(); }
    bool isProgressionFull() const noexcept           { return numProgressionChords.load() >= smartkeys::ProgressionModel::kMaxBlocks; }
    juce::File exportProgression (const juce::String& baseName);

    // Escuchar un acorde de la paleta / del carril (mientras se mantiene pulsado)
    void auditionChord (const int* notes, int count, int velocity, bool isOn) noexcept;

    // Vista previa de la progresión
    void startPreview() noexcept              { previewRequest.store (1); }
    void stopPreview() noexcept               { previewRequest.store (2); }
    bool isPreviewPlaying() const noexcept    { return previewPlayingAtomic.load(); }
    double getPreviewPositionPpq() const noexcept { return previewPosAtomic.load(); }

    // Captura
    void setRecArmed (bool armed) noexcept    { recArmed.store (armed); }
    bool isRecArmed() const noexcept          { return recArmed.load(); }
    bool isCapturing() const noexcept         { return capturingNow.load(); }
    bool isCapturingFromHost() const noexcept { return captureFromHost.load(); }
    bool isHostPlaying() const noexcept       { return hostPlaying.load(); }
    int  getCaptureSync() const noexcept      { return captureSync.load(); }
    void setCaptureSync (int s) noexcept      { captureSync.store (juce::jlimit (0, 2, s)); }
    int  getTakeMode() const noexcept         { return takeMode.load(); }
    void setTakeMode (int m) noexcept         { takeMode.store (juce::jlimit (0, 1, m)); }

    // Ajustes del carril
    int  getQuantizeIndex() const noexcept    { return quantizeIndex.load(); }
    void setQuantizeIndex (int i) noexcept    { quantizeIndex.store (juce::jlimit (0, 6, i)); }
    int  getNewChordLengthIndex() const noexcept { return newChordLengthIndex.load(); }
    void setNewChordLengthIndex (int i) noexcept { newChordLengthIndex.store (juce::jlimit (0, 3, i)); }
    bool getShowRoman() const noexcept        { return showRoman.load(); }
    void setShowRoman (bool r) noexcept       { showRoman.store (r); }
    float getEditorScale() const noexcept     { return editorScale.load(); }
    void setEditorScale (float s) noexcept    { editorScale.store (juce::jlimit (0.6f, 2.0f, s)); }

    double getHostBpm() const noexcept        { return hostBpm.load(); }
    double getBeatsPerBar() const noexcept;
    int getTimeSigNumerator() const noexcept  { return hostTimeSigNum.load(); }
    int getTimeSigDenominator() const noexcept{ return hostTimeSigDen.load(); }
    double getStrumMs() const noexcept        { return pStrumMs->load(); }
    bool isStrumDescending() const noexcept   { return juce::roundToInt (pStrumDir->load()) == StrumDir::Descending; }

    static juce::StringArray getKeyNames();

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void updateEngineIfNeeded();
    void drainScreenNotes (juce::MidiBuffer& out, int outChannel, double ppq, double bpm);
    void drainPaletteEvents (juce::MidiBuffer& out, int outChannel);
    void openHardwareMicIfNeeded();
    void rebuildPreviewSnapshot();
    void drainCaptureEvents();

    // Captura (hilo de audio)
    enum class CaptureBase { None, Host, Free };
    void updateCaptureState (bool hostRunning, bool hostRecording, bool hasPpq, double ppqAtBlockStart);
    void startTake (double startPpq);
    void endTake();
    void newPass (double startPpq);
    void flushRegion (double upToPpq);
    void captureChordOn (const smartkeys::MidiOutputRouter::ChordTriggerInfo& info, double ppq, double bpm);
    void captureChordOff (uint32_t chordId, double ppq);
    void captureCloseAll (double ppq);

    // Vista previa (hilo de audio)
    void runPreview (juce::MidiBuffer& out, int outChannel, int numSamples, double ppqPerSample, bool hostStarted);
    void stopPreviewNow (int samplePosition, juce::MidiBuffer& out);

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
    std::atomic<float>* pStrumMs     = nullptr;
    std::atomic<float>* pStrumDir    = nullptr;

    smartkeys::ScaleChordEngine  engine;
    smartkeys::VoicePitchTracker dawTracker;        // vía A: bus de entrada del DAW
    smartkeys::VoicePitchTracker hardwareTracker;   // vía B: micrófono directo
    smartkeys::FlKeysPianoSynth  pianoSynth;
    smartkeys::MidiOutputRouter  router;
    smartkeys::StrumScheduler    strum;
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
    bool     hostWasRunning = false;
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

    // FIFO de la paleta de acordes (escuchar acordes explícitos)
    struct PaletteEvent
    {
        bool on = true;
        int count = 0;
        std::array<int, smartkeys::ChordBlock::kMaxNotes> notes {};
        std::array<juce::uint8, smartkeys::ChordBlock::kMaxNotes> velocities {};
    };
    static constexpr int kPaletteFifoSize = 64;
    juce::AbstractFifo paletteFifo { kPaletteFifoSize };
    std::array<PaletteEvent, kPaletteFifoSize> paletteEvents {};

    std::atomic<bool> panicRequested { false };
    std::array<std::atomic<uint64_t>, 2> activeNotesBitmask {};

    // Transporte del host
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int>    hostTimeSigNum { 4 }, hostTimeSigDen { 4 };
    std::atomic<bool>   hostPlaying { false };

    // Captura (estado del hilo de audio)
    smartkeys::CaptureFifo captureFifo;
    CaptureBase captureBase = CaptureBase::None;
    int32_t capturePassId = 0;
    double freeClockPpq = 0.0, lastHostBlockEndPpq = 0.0, regionStartPpq = 0.0;
    std::atomic<bool>   recArmed { false }, capturingNow { false }, captureFromHost { false };
    std::atomic<int>    captureSync { CaptureSync::PlayOrRecord }, takeMode { TakeMode::Replace };

    // Progresión (hilo de mensajes, con candado)
    juce::CriticalSection progressionLock;
    smartkeys::ProgressionModel progression;
    std::atomic<uint32_t> progressionVersion { 1 };
    std::atomic<int> numProgressionChords { 0 };
    uint32_t snapshotBuiltForVersion = 0;
    std::atomic<int> quantizeIndex { smartkeys::QuantizeGrid::kDefault };
    std::atomic<int> newChordLengthIndex { smartkeys::NewChordLength::kDefault };
    std::atomic<bool> showRoman { false };
    std::atomic<float> editorScale { 1.0f };

    // Vista previa: instantánea compartida (candado sin espera) + copia del hilo de audio
    juce::SpinLock snapshotLock;
    smartkeys::PreviewSnapshot sharedSnapshot, audioSnapshot;
    std::atomic<bool> snapshotDirty { true };
    std::atomic<int> previewRequest { 0 };      // 1 = reproducir, 2 = detener
    std::atomic<bool> previewPlayingAtomic { false };
    std::atomic<double> previewPosAtomic { 0.0 };
    bool previewPlaying = false;
    double previewPos = 0.0;
    int previewNext = 0, previewActive = -1;

    juce::MidiBuffer routedMidi;                  // acordes completos (antes del STRUM)
    juce::MidiBuffer expandedChordMidi;           // salida final (con STRUM)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoothed;

    static constexpr size_t kMidiReserveBytes = 16384;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessor)
};
