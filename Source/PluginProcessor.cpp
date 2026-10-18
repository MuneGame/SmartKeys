/*
  ==============================================================================

    PluginProcessor.cpp
    SmartKeys — Piano inteligente, generador de acordes y controlador vocal.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace smartkeys;

//==============================================================================
juce::StringArray SmartKeysAudioProcessor::getKeyNames()
{
    return { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
}

juce::AudioProcessorValueTreeState::ParameterLayout SmartKeysAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::refPitch, 1 }, "Reference Pitch",
        NormalisableRange<float> (400.0f, 480.0f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::rootKey, 1 }, "Key", getKeyNames(), 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::scale, 1 }, "Scale", ScaleChordEngine::getAllScaleNames(), 0));

    // El orden se conserva (0 = Smart, 1 = Single) para que los estados guardados sigan siendo válidos
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "Mode",
        StringArray { "Smart Chords", "Single Notes", "Complex Chords" }, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::chordExt, 1 }, "Chord Type",
        StringArray { "Triad", "Triad + Octave", "Seventh" }, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::complexVar, 2 }, "Complex Variation",
        ScaleChordEngine::getComplexVariationNames(), 0));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::micOn, 1 }, "Mic Voice Controller", false));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::micSource, 3 }, "Mic Source",
        StringArray { "DAW Input", "Hardware Mic" }, MicSource::DawInput));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::micGate, 1 }, "Mic Gate",
        NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -48.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    // 1.2: la salida MIDI de los acordes está SIEMPRE activa; sólo se elige el piano interno
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::outputMode, 4 }, "Output Mode",
        StringArray { "Piano + MIDI Out", "MIDI Out Only" }, OutputMode::PianoAndMidi));

    StringArray channels;
    for (int ch = 1; ch <= 16; ++ch)
        channels.add (String (ch));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::midiOutCh, 3 }, "MIDI Out Channel", channels, 0));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::masterGain, 1 }, "Master Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::microTuning, 1 }, "Scale Microtuning", false));

    // STRUM: retardo progresivo entre las notas del acorde (0 = todas juntas)
    NormalisableRange<float> strumRange (0.0f, (float) StrumScheduler::kMaxStrumMs, 0.1f);
    strumRange.setSkewForCentre (60.0f);
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::strumMs, 4 }, "Strum", strumRange, 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int)
            {
                if (v < 0.05f) return String ("OFF");
                return (v < 10.0f ? String (v, 1) : String (roundToInt (v))) + " ms";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.trim().equalsIgnoreCase ("OFF") ? 0.0f : jlimit (0.0f, (float) StrumScheduler::kMaxStrumMs, text.getFloatValue());
            })));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::strumDir, 4 }, "Strum Direction",
        StringArray { "Ascending", "Descending" }, StrumDir::Ascending));

    return { params.begin(), params.end() };
}

// Vía A: bus de entrada de micrófono (mono o estéreo) + salida estéreo del piano
juce::AudioProcessor::BusesProperties SmartKeysAudioProcessor::createBuses()
{
    return BusesProperties()
        .withInput  ("Mic Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true);
}

//==============================================================================
SmartKeysAudioProcessor::SmartKeysAudioProcessor()
    : AudioProcessor (createBuses()),
      apvts (*this, nullptr, "SmartKeysState", createParameterLayout())
{
    pRefPitch    = apvts.getRawParameterValue (ParamIDs::refPitch);
    pRootKey     = apvts.getRawParameterValue (ParamIDs::rootKey);
    pScale       = apvts.getRawParameterValue (ParamIDs::scale);
    pMode        = apvts.getRawParameterValue (ParamIDs::mode);
    pChordExt    = apvts.getRawParameterValue (ParamIDs::chordExt);
    pComplexVar  = apvts.getRawParameterValue (ParamIDs::complexVar);
    pMicOn       = apvts.getRawParameterValue (ParamIDs::micOn);
    pMicSource   = apvts.getRawParameterValue (ParamIDs::micSource);
    pMicGate     = apvts.getRawParameterValue (ParamIDs::micGate);
    pOutputMode  = apvts.getRawParameterValue (ParamIDs::outputMode);
    pMidiOutCh   = apvts.getRawParameterValue (ParamIDs::midiOutCh);
    pMasterGain  = apvts.getRawParameterValue (ParamIDs::masterGain);
    pMicroTuning = apvts.getRawParameterValue (ParamIDs::microTuning);
    pStrumMs     = apvts.getRawParameterValue (ParamIDs::strumMs);
    pStrumDir    = apvts.getRawParameterValue (ParamIDs::strumDir);

    apvts.addParameterListener (ParamIDs::rootKey, this);
    apvts.addParameterListener (ParamIDs::scale, this);

    customMask.store (ScaleChordEngine::getPitchClassMask (0, 0));

    for (auto& a : activeNotesBitmask)
        a.store (0);

    routedMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.ensureSize (kMidiReserveBytes);
    rebuildPreviewSnapshot();
    startTimerHz (25);
}

SmartKeysAudioProcessor::~SmartKeysAudioProcessor()
{
    stopTimer();
    hardwareMic.close();
    apvts.removeParameterListener (ParamIDs::rootKey, this);
    apvts.removeParameterListener (ParamIDs::scale, this);
}

//==============================================================================
// Al elegir otra tónica o escala predefinida, la máscara personalizada se
// sustituye por la de la escala. Sólo operaciones atómicas: es seguro tanto si
// el cambio llega desde la interfaz como desde la automatización del host.
void SmartKeysAudioProcessor::parameterChanged (const juce::String&, float)
{
    const int root  = (int) pRootKey->load();
    const int scale = (int) pScale->load();
    customMask.store (ScaleChordEngine::getPitchClassMask (root, scale));
}

void SmartKeysAudioProcessor::toggleCustomNote (int pitchClass) noexcept
{
    pitchClass = ((pitchClass % 12) + 12) % 12;
    uint16_t current = customMask.load();
    for (;;)
    {
        const uint16_t next = (uint16_t) (current ^ (1u << (unsigned) pitchClass));
        if (next == 0)
            return;   // nunca dejar la escala vacía
        if (customMask.compare_exchange_weak (current, next))
            return;
    }
}

void SmartKeysAudioProcessor::pushScreenNote (int note, int velocity, bool isNoteOn) noexcept
{
    const auto scope = screenFifo.write (1);
    const ScreenNoteEvent e { juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity), isNoteOn };
    if (scope.blockSize1 > 0)      screenEvents[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) screenEvents[(size_t) scope.startIndex2] = e;
}

void SmartKeysAudioProcessor::auditionChord (const int* notes, int count, int velocity, bool isOn) noexcept
{
    PaletteEvent e;
    e.on = isOn;
    if (isOn)
    {
        for (int i = 0; i < count && e.count < ChordBlock::kMaxNotes; ++i)
        {
            e.notes[(size_t) e.count] = juce::jlimit (0, 127, notes[i]);
            e.velocities[(size_t) e.count] = (juce::uint8) juce::jlimit (1, 127, velocity);
            ++e.count;
        }
    }

    const auto scope = paletteFifo.write (1);
    if (scope.blockSize1 > 0)      paletteEvents[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) paletteEvents[(size_t) scope.startIndex2] = e;
}

double SmartKeysAudioProcessor::getBeatsPerBar() const noexcept
{
    const int num = juce::jlimit (1, 32, hostTimeSigNum.load());
    const int den = juce::jlimit (1, 32, hostTimeSigDen.load());
    return (double) num * 4.0 / (double) den;
}

//==============================================================================
// Hilo de mensajes: captura de la progresión + micrófono de hardware
void SmartKeysAudioProcessor::timerCallback()
{
    drainCaptureEvents();
    openHardwareMicIfNeeded();
}

void SmartKeysAudioProcessor::drainCaptureEvents()
{
    const double beatsPerBar = getBeatsPerBar();
    bool any = false;
    {
        const juce::ScopedLock sl (progressionLock);
        captureFifo.drain ([this, beatsPerBar, &any] (const CaptureEvent& e)
        {
            progression.applyCaptureEvent (e, beatsPerBar);
            any = true;
        });
    }
    if (any)
        progressionEdited();
}

void SmartKeysAudioProcessor::progressionEdited()
{
    rebuildPreviewSnapshot();
}

void SmartKeysAudioProcessor::rebuildPreviewSnapshot()
{
    const double beatsPerBar = getBeatsPerBar();
    const juce::ScopedLock sl (progressionLock);

    const auto version = progression.getVersion();
    numProgressionChords.store (progression.size());
    if (version == snapshotBuiltForVersion)
        return;

    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        progression.fillSnapshot (sharedSnapshot, beatsPerBar);
    }
    snapshotBuiltForVersion = version;
    snapshotDirty.store (true);
    progressionVersion.store (version);
}

juce::File SmartKeysAudioProcessor::exportProgression (const juce::String& baseName)
{
    drainCaptureEvents();
    const juce::ScopedLock sl (progressionLock);
    return progression.writeMidiFile (baseName, hostBpm.load(), QuantizeGrid::getGridPpq (quantizeIndex.load()),
                                      getStrumMs(), isStrumDescending(),
                                      hostTimeSigNum.load(), hostTimeSigDen.load());
}

void SmartKeysAudioProcessor::openHardwareMicIfNeeded()
{
    // Un proyecto cargado con otro dispositivo: cerrar el actual y abrir el guardado
    if (hardwareReopenRequested.exchange (false))
        hardwareMic.close();

    const bool wantDevice = juce::roundToInt (pMicSource->load()) == MicSource::HardwareDevice;

    if (! wantDevice)
    {
        if (hardwareMic.isOpen())
            hardwareMic.close();
        const juce::ScopedLock sl (hardwareStateLock);
        hardwareStatus.clear();
        hardwareOpenFailed = false;
        return;
    }

    // Sólo se abre un dispositivo elegido por el usuario (o restaurado del proyecto)
    juce::String deviceName;
    {
        const juce::ScopedLock sl (hardwareStateLock);
        if (hardwareMic.isOpen() || hardwareOpenFailed || hardwareDeviceName.isEmpty())
            return;
        deviceName = hardwareDeviceName;
    }

    const auto error = hardwareMic.open (deviceName, (HardwareMicInput::ChannelMode) hardwareChannelMode.load());

    const juce::ScopedLock sl (hardwareStateLock);
    hardwareStatus = error;
    hardwareOpenFailed = error.isNotEmpty();
}

void SmartKeysAudioProcessor::setHardwareInput (const juce::String& deviceName, int channelMode, bool activate)
{
    hardwareChannelMode.store (juce::jlimit (0, 2, channelMode));
    hardwareMic.setChannelMode ((HardwareMicInput::ChannelMode) hardwareChannelMode.load());

    auto setParam = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    if (activate)
    {
        setParam (ParamIDs::micSource, (float) MicSource::HardwareDevice);
        setParam (ParamIDs::micOn, 1.0f);
    }

    const bool sameDevice = hardwareMic.isOpen() && hardwareMic.getOpenDeviceName() == deviceName;
    {
        const juce::ScopedLock sl (hardwareStateLock);
        hardwareDeviceName = deviceName;
        if (! sameDevice)
        {
            hardwareOpenFailed = false;
            hardwareStatus.clear();
        }
    }
    if (sameDevice)
        return;   // sólo cambió el canal

    hardwareMic.close();
    openHardwareMicIfNeeded();
}

void SmartKeysAudioProcessor::retryHardwareInput()
{
    hardwareMic.close();
    {
        const juce::ScopedLock sl (hardwareStateLock);
        hardwareOpenFailed = false;
        hardwareStatus.clear();
    }
    openHardwareMicIfNeeded();
}

//==============================================================================
void SmartKeysAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    pianoSynth.prepare (currentSampleRate, samplesPerBlock);
    dawTracker.prepare (currentSampleRate);
    if (hardwareMic.isOpen())
    {
        hardwareMic.consumeRestartFlag();
        hardwareTracker.prepare (hardwareMic.getDeviceSampleRate());
    }
    hardwareScratch.assign ((size_t) juce::jmax (16384, samplesPerBlock * 8), 0.0f);

    router.reset();
    strum.prepare (currentSampleRate);
    routedMidi.ensureSize (kMidiReserveBytes);
    routedMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();

    gainSmoothed.reset (currentSampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));

    appliedRoot = appliedScale = appliedMode = appliedExt = appliedVar = -1;
    appliedRef = 0.0f;
    micWasOn = false;
    lastVoiceSource = -1;
    pianoWasActive = juce::roundToInt (pOutputMode->load()) == OutputMode::PianoAndMidi;

    // Captura y vista previa vuelven a empezar
    if (captureBase != CaptureBase::None)
        endTake();
    captureBase = CaptureBase::None;
    capturingNow.store (false);
    previewPlaying = false;
    previewActive = -1;
    previewPlayingAtomic.store (false);
    snapshotDirty.store (true);

    updateEngineIfNeeded();
}

void SmartKeysAudioProcessor::releaseResources()
{
    dawTracker.reset();
    hardwareTracker.reset();
    pianoSynth.allNotesOff();
    strum.reset();
}

bool SmartKeysAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Entrada de micrófono: mono o estéreo. En el instrumento es un sidechain y
    // puede desactivarse; en SmartKeys FX es la entrada principal de la pista.
    if (layouts.inputBuses.size() > 0)
    {
        const auto in = layouts.getChannelSet (true, 0);
        if (in.isDisabled())
            return ! isFxVariant;
        if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void SmartKeysAudioProcessor::updateEngineIfNeeded()
{
    const int root   = juce::jlimit (0, 11, (int) pRootKey->load());
    const int scale  = juce::jlimit (0, ScaleChordEngine::getNumScales() - 1, (int) pScale->load());
    const uint16_t mask = customMask.load();
    const int mode   = juce::jlimit (0, 2, (int) pMode->load());
    const int ext    = juce::jlimit (0, 2, (int) pChordExt->load());
    const int var    = juce::jlimit (0, 2, (int) pComplexVar->load());
    const float ref  = pRefPitch->load();
    const bool micro = pMicroTuning->load() > 0.5f;

    const bool scaleChanged = (root != appliedRoot || scale != appliedScale || mask != appliedMask);

    if (scaleChanged)
        engine.setState (root, scale, mask);   // reconstruye tablas sin asignar memoria

    if (mode != appliedMode)
        engine.setMode (static_cast<OperationMode> (mode));   // 0 Smart, 1 Single, 2 Complex

    if (ext != appliedExt)
        engine.setChordExtension (static_cast<ChordExtension> (ext));

    if (var != appliedVar)
        engine.setComplexVariation (static_cast<ComplexVariation> (var));

    if (scaleChanged || std::abs (ref - appliedRef) > 1.0e-4f || micro != appliedMicro)
    {
        engine.setReferencePitch (ref);
        for (int n = 0; n < 128; ++n)
            pianoSynth.setNoteFrequency (n, engine.getTunedFrequency (n, micro));
    }

    appliedRoot = root;   appliedScale = scale; appliedMask = mask;
    appliedMode = mode;   appliedExt = ext;     appliedVar = var;
    appliedRef = ref;     appliedMicro = micro;
}

void SmartKeysAudioProcessor::drainScreenNotes (juce::MidiBuffer& out, int outChannel, double ppq, double bpm)
{
    const int ready = screenFifo.getNumReady();
    if (ready <= 0)
        return;

    const auto scope = screenFifo.read (ready);
    auto handle = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& e = screenEvents[(size_t) (start + i)];
            if (e.on)
            {
                MidiOutputRouter::ChordTriggerInfo info;
                router.handleNoteOn (MidiOutputRouter::Source::Screen, e.note, outChannel, (juce::uint8) e.velocity,
                                     0, engine, out, &info);
                captureChordOn (info, ppq, bpm);
            }
            else
            {
                captureChordOff (router.handleNoteOff (MidiOutputRouter::Source::Screen, e.note, 0, out), ppq);
            }
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

void SmartKeysAudioProcessor::drainPaletteEvents (juce::MidiBuffer& out, int outChannel)
{
    const int ready = paletteFifo.getNumReady();
    if (ready <= 0)
        return;

    const auto scope = paletteFifo.read (ready);
    auto handle = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& e = paletteEvents[(size_t) (start + i)];
            if (e.on && e.count > 0)
                router.handleExplicitChordOn (MidiOutputRouter::Source::Palette, e.notes.data(), e.velocities.data(),
                                              e.count, outChannel, 0, out);
            else
                router.handleExplicitChordOff (MidiOutputRouter::Source::Palette, 0, out);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

//==============================================================================
// CAPTURA (hilo de audio). Sólo se capturan acordes mientras el DAW reproduce
// o graba (según el ajuste) o con el botón REC armado; con el DAW detenido,
// probar teclas no guarda nada.
void SmartKeysAudioProcessor::updateCaptureState (bool hostRunning, bool hostRecording, bool hasPpq, double ppq0)
{
    const int sync = captureSync.load();
    const bool armed = recArmed.load();

    const bool wantsHost = (sync == CaptureSync::PlayOrRecord && hostRunning)
                        || (sync == CaptureSync::RecordOnly && hasPpq && hostRecording)
                        || (armed && hostRunning);
    const CaptureBase base = wantsHost ? CaptureBase::Host : (armed ? CaptureBase::Free : CaptureBase::None);

    if (base != captureBase)
    {
        if (captureBase != CaptureBase::None)
            endTake();
        captureBase = base;
        if (base != CaptureBase::None)
            startTake (base == CaptureBase::Host ? ppq0 : 0.0);
    }
    else if (base == CaptureBase::Host)
    {
        // Bucle del patrón o salto del cursor: nueva vuelta (sobrescribe lo que se repite)
        if (ppq0 < lastHostBlockEndPpq - 1.0e-3 || ppq0 > lastHostBlockEndPpq + 0.5)
            newPass (ppq0);
    }
}

void SmartKeysAudioProcessor::startTake (double startPpq)
{
    ++capturePassId;
    if (captureBase == CaptureBase::Free)
        freeClockPpq = 0.0;
    lastHostBlockEndPpq = startPpq;
    regionStartPpq = startPpq;

    CaptureEvent e;
    e.type = CaptureEvent::TakeStart;
    e.passId = capturePassId;
    e.replace = takeMode.load() == TakeMode::Replace;
    e.freeClock = captureBase == CaptureBase::Free;
    e.ppq = startPpq;
    captureFifo.push (e);
}

void SmartKeysAudioProcessor::endTake()
{
    const double endPpq = captureBase == CaptureBase::Host ? lastHostBlockEndPpq : freeClockPpq;
    flushRegion (endPpq);

    CaptureEvent e;
    e.type = CaptureEvent::TakeEnd;
    e.passId = capturePassId;
    e.ppq = endPpq;
    captureFifo.push (e);
}

void SmartKeysAudioProcessor::newPass (double startPpq)
{
    flushRegion (lastHostBlockEndPpq);
    ++capturePassId;

    CaptureEvent e;
    e.type = CaptureEvent::PassStart;
    e.passId = capturePassId;
    e.ppq = startPpq;
    captureFifo.push (e);

    regionStartPpq = startPpq;
    lastHostBlockEndPpq = startPpq;
}

void SmartKeysAudioProcessor::flushRegion (double upToPpq)
{
    if (captureBase == CaptureBase::None || upToPpq <= regionStartPpq + 1.0e-9)
        return;

    CaptureEvent e;
    e.type = CaptureEvent::Region;
    e.passId = capturePassId;
    e.ppq = regionStartPpq;
    e.ppqEnd = upToPpq;
    captureFifo.push (e);
    regionStartPpq = upToPpq;
}

void SmartKeysAudioProcessor::captureChordOn (const MidiOutputRouter::ChordTriggerInfo& info, double ppq, double bpm)
{
    if (captureBase == CaptureBase::None)
        return;

    if (info.releasedChordId != 0)
        captureChordOff (info.releasedChordId, ppq);

    if (info.chordId == 0 || info.count == 0)
        return;

    flushRegion (ppq);

    CaptureEvent e;
    e.type = CaptureEvent::ChordOn;
    e.passId = capturePassId;
    e.chordId = info.chordId;
    e.ppq = ppq;
    e.count = (uint8_t) juce::jmin (info.count, CaptureEvent::kMaxNotes);
    for (int i = 0; i < (int) e.count; ++i)
    {
        e.notes[(size_t) i] = (int8_t) info.notes[(size_t) i];
        e.velocities[(size_t) i] = info.velocities[(size_t) i];
    }
    e.strumMs = (float) strum.getStrumMs();
    e.strumDescending = strum.isDescending();
    e.bpm = (float) bpm;
    captureFifo.push (e);
}

void SmartKeysAudioProcessor::captureChordOff (uint32_t chordId, double ppq)
{
    if (captureBase == CaptureBase::None || chordId == 0)
        return;

    flushRegion (ppq);

    CaptureEvent e;
    e.type = CaptureEvent::ChordOff;
    e.passId = capturePassId;
    e.chordId = chordId;
    e.ppq = ppq;
    captureFifo.push (e);
}

void SmartKeysAudioProcessor::captureCloseAll (double ppq)
{
    if (captureBase == CaptureBase::None)
        return;

    flushRegion (ppq);

    CaptureEvent e;
    e.type = CaptureEvent::CloseAll;
    e.passId = capturePassId;
    e.ppq = ppq;
    captureFifo.push (e);
}

//==============================================================================
// VISTA PREVIA de la progresión (hilo de audio): toca los acordes del carril al
// tempo del proyecto, en bucle, por el piano interno y la salida MIDI.
void SmartKeysAudioProcessor::stopPreviewNow (int samplePosition, juce::MidiBuffer& out)
{
    if (router.isExplicitSlotActive (MidiOutputRouter::Source::Preview))
        router.handleExplicitChordOff (MidiOutputRouter::Source::Preview, samplePosition, out);
    previewActive = -1;
    previewPlaying = false;
    previewPlayingAtomic.store (false);
}

void SmartKeysAudioProcessor::runPreview (juce::MidiBuffer& out, int outChannel, int numSamples,
                                          double ppqPerSample, bool hostStarted)
{
    const int request = previewRequest.exchange (0);
    if (request == 2 || hostStarted)
        stopPreviewNow (0, out);

    // Nueva instantánea de la progresión (candado sin espera: si está ocupado, en el siguiente bloque)
    if (snapshotDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (snapshotLock);
        if (lock.isLocked())
        {
            audioSnapshot = sharedSnapshot;
            snapshotDirty.store (false);

            if (previewPlaying)
            {
                if (previewActive >= 0)
                {
                    router.handleExplicitChordOff (MidiOutputRouter::Source::Preview, 0, out);
                    previewActive = -1;
                }
                previewNext = 0;
                while (previewNext < audioSnapshot.numChords
                       && audioSnapshot.chords[(size_t) previewNext].start < previewPos - 1.0e-9)
                    ++previewNext;
            }
        }
    }

    if (request == 1 && ! hostStarted)
    {
        if (previewActive >= 0)
            router.handleExplicitChordOff (MidiOutputRouter::Source::Preview, 0, out);
        previewPlaying = true;
        previewPos = 0.0;
        previewNext = 0;
        previewActive = -1;
    }

    if (! previewPlaying)
        return;

    const auto& snap = audioSnapshot;
    if (snap.numChords <= 0 || ppqPerSample <= 0.0)
    {
        stopPreviewNow (0, out);
        return;
    }

    const double loopLength = juce::jmax (1.0, snap.loopLength);
    const int lastSample = juce::jmax (0, numSamples - 1);
    double t0 = previewPos;
    double t1 = previewPos + (double) numSamples * ppqPerSample;

    for (int guard = 0; guard < 4096; ++guard)
    {
        const double tOff = previewActive >= 0 ? snap.chords[(size_t) previewActive].end : 1.0e300;
        const double tOn  = previewNext < snap.numChords ? snap.chords[(size_t) previewNext].start : 1.0e300;
        const double t = juce::jmin (tOff, tOn, loopLength);
        if (t >= t1)
            break;

        const int pos = juce::jlimit (0, lastSample, (int) std::floor ((t - t0) / ppqPerSample));

        if (tOff <= tOn && tOff <= loopLength)
        {
            router.handleExplicitChordOff (MidiOutputRouter::Source::Preview, pos, out);
            previewActive = -1;
        }
        else if (tOn <= loopLength)
        {
            const auto& c = snap.chords[(size_t) previewNext];
            router.handleExplicitChordOn (MidiOutputRouter::Source::Preview, c.notes.data(), c.velocities.data(),
                                          c.count, outChannel, pos, out);
            previewActive = previewNext;
            ++previewNext;
        }
        else
        {
            // Fin del bucle: vuelve al compás 1
            if (previewActive >= 0)
                router.handleExplicitChordOff (MidiOutputRouter::Source::Preview, pos, out);
            previewActive = -1;
            previewNext = 0;
            t0 -= loopLength;
            t1 -= loopLength;
        }
    }

    previewPos = t1;
    previewPosAtomic.store (previewPos);
    previewPlayingAtomic.store (true);
}

//==============================================================================
void SmartKeysAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    updateEngineIfNeeded();
    const float gateDb = pMicGate->load();
    dawTracker.setGateThresholdDb (gateDb);
    hardwareTracker.setGateThresholdDb (gateDb);
    strum.setStrum ((double) pStrumMs->load(), juce::roundToInt (pStrumDir->load()) == StrumDir::Descending);

    // Capacidad reservada: tras el primer bloque no hay asignaciones
    routedMidi.clear();
    routedMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);

    //==========================================================================
    // TRANSPORTE DEL DAW: tempo, compás, reproducción / grabación y posición
    //==========================================================================
    double bpm = hostBpm.load();
    bool playing = false, recording = false, hasPpq = false;
    double ppq0 = 0.0;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto hostTempo = position->getBpm())
                if (*hostTempo > 20.0 && *hostTempo < 400.0)
                    bpm = *hostTempo;
            playing = position->getIsPlaying();
            recording = position->getIsRecording();
            if (auto p = position->getPpqPosition())
            {
                hasPpq = true;
                ppq0 = *p;
            }
            if (auto ts = position->getTimeSignature())
            {
                if (ts->numerator > 0 && ts->denominator > 0)
                {
                    hostTimeSigNum.store (ts->numerator);
                    hostTimeSigDen.store (ts->denominator);
                }
            }
        }
    }
    hostBpm.store (bpm);
    hostPlaying.store (playing || recording);

    const double ppqPerSample = bpm / (60.0 * juce::jmax (1.0, currentSampleRate));
    const bool hostRunning = hasPpq && (playing || recording);
    const bool hostStarted = hostRunning && ! hostWasRunning;
    hostWasRunning = hostRunning;

    // Captura: decide si esta parte de la música se guarda en la progresión
    updateCaptureState (hostRunning, recording, hasPpq, ppq0);
    const double blockStartPpq = captureBase == CaptureBase::Host ? ppq0 : freeClockPpq;
    auto ppqAt = [blockStartPpq, ppqPerSample] (int samplePos) { return blockStartPpq + (double) samplePos * ppqPerSample; };

    //==========================================================================
    // PASO 1 — LEER EL MICRÓFONO Y DETECTAR LA VOZ (antes de tocar el buffer)
    //==========================================================================
    const bool micOn  = pMicOn->load() > 0.5f;
    const int  source = juce::jlimit (0, 1, juce::roundToInt (pMicSource->load()));

    if (source != lastVoiceSource)
    {
        // Cambio de fuente: soltar el acorde vocal que estuviera sonando
        captureChordOff (router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, routedMidi), ppqAt (0));
        dawTracker.forceRelease (0);
        hardwareTracker.forceRelease (0);
        lastVoiceSource = source;
    }

    if (! micOn && micWasOn)
        captureChordOff (router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, routedMidi), ppqAt (0));
    if (micOn && ! micWasOn)
    {
        // Al encender, la siguiente nota estable que cantes dispara su acorde
        dawTracker.forceRelease (0);
        hardwareTracker.forceRelease (0);
    }
    micWasOn = micOn;

    VoicePitchTracker* tracker = nullptr;
    int numVoiceEvents = 0;
    int voiceSamples = juce::jmax (1, numSamples);   // escala de los offsets de los eventos de voz

    if (source == MicSource::DawInput)
    {
        // Vía A: bus de entrada del DAW (sidechain en el instrumento, entrada principal en FX)
        if (getBusCount (true) > 0)
        {
            if (auto* bus = getBus (true, 0); bus != nullptr && bus->isEnabled())
            {
                auto micBus = getBusBuffer (buffer, true, 0);
                if (micBus.getNumChannels() > 0)
                {
                    numVoiceEvents = dawTracker.process (micBus.getArrayOfReadPointers(),
                                                         micBus.getNumChannels(), numSamples, engine);
                    tracker = &dawTracker;
                }
            }
        }
    }
    else
    {
        // Vía B: micrófono leído directamente de la interfaz de audio
        if (hardwareMic.consumeRestartFlag())
            hardwareTracker.prepare (hardwareMic.getDeviceSampleRate());

        if (hardwareMic.isOpen() && ! hardwareScratch.empty())
        {
            const double deviceRate = hardwareMic.getDeviceSampleRate();
            const int maxLatency = (int) juce::jmax (0.06 * deviceRate,
                                                     2.5 * (double) numSamples / currentSampleRate * deviceRate);
            const int pulled = hardwareMic.pullSamples (hardwareScratch.data(), (int) hardwareScratch.size(), maxLatency);
            const float* mono[1] = { hardwareScratch.data() };
            numVoiceEvents = hardwareTracker.process (mono, 1, pulled, engine);
            voiceSamples = juce::jmax (1, pulled);
            tracker = &hardwareTracker;
        }
    }

    //==========================================================================
    // PASO 2 — MEDIDOR + NOTA DETECTADA EN VIVO (atómicos que lee la interfaz)
    //==========================================================================
    activeVoiceSource.store (source);
    micSourceAvailable.store (tracker != nullptr);
    micLevelDb.store (tracker != nullptr ? tracker->inputLevelDb.load() : -100.0f);

    //==========================================================================
    // PASO 3 — SILENCIAR LA VOZ CRUDA (ya analizada) ANTES DE RENDERIZAR EL PIANO
    //==========================================================================
    buffer.clear();

    //==========================================================================
    // PASO 4 — EXPANDIR CADA NOTA DE ENTRADA AL ACORDE COMPLETO
    //          (MIDI, Piano Roll, teclado en pantalla / PC, voz, paleta y vista
    //          previa, en orden de muestra) y capturarlo si el DAW graba
    //==========================================================================
    const int outChannel = 1 + juce::jlimit (0, 15, juce::roundToInt (pMidiOutCh->load()));

    if (panicRequested.exchange (false))
    {
        router.releaseAll (0, routedMidi);
        captureCloseAll (ppqAt (0));
    }

    drainPaletteEvents (routedMidi, outChannel);
    drainScreenNotes (routedMidi, outChannel, ppqAt (0), bpm);
    runPreview (routedMidi, outChannel, numSamples, ppqPerSample, hostStarted);

    const int routedVoiceEvents = (micOn && tracker != nullptr) ? numVoiceEvents : 0;
    auto voicePosition = [&] (int index)
    {
        const auto offset = (juce::int64) tracker->getEvent (index).sampleOffset;
        return juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) (offset * numSamples / voiceSamples));
    };

    auto midiIt = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    int voiceIdx = 0;

    while (midiIt != midiEnd || voiceIdx < routedVoiceEvents)
    {
        const bool takeMidi = (midiIt != midiEnd)
                              && (voiceIdx >= routedVoiceEvents || (*midiIt).samplePosition <= voicePosition (voiceIdx));

        if (takeMidi)
        {
            const auto meta = *midiIt;
            const auto msg = meta.getMessage();
            const int pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);

            if (msg.isNoteOn())
            {
                MidiOutputRouter::ChordTriggerInfo info;
                router.handleNoteOn (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), outChannel,
                                     msg.getVelocity(), pos, engine, routedMidi, &info);
                captureChordOn (info, ppqAt (pos), bpm);
            }
            else if (msg.isNoteOff())
            {
                captureChordOff (router.handleNoteOff (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), pos, routedMidi),
                                 ppqAt (pos));
            }
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    router.releaseAll (pos, routedMidi);
                    captureCloseAll (ppqAt (pos));
                }

                if (msg.getChannel() > 0)
                {
                    auto routed = msg;              // CC, sustain, pitch bend... al canal MIDI elegido
                    routed.setChannel (outChannel);
                    routedMidi.addEvent (routed, pos);
                }
                else
                {
                    routedMidi.addEvent (msg, pos);
                }
            }
            ++midiIt;
        }
        else
        {
            const auto& e = tracker->getEvent (voiceIdx);
            const int pos = voicePosition (voiceIdx);

            if (e.isNoteOn)
            {
                MidiOutputRouter::ChordTriggerInfo info;
                router.handleNoteOn (MidiOutputRouter::Source::Voice, e.note, outChannel, (juce::uint8) e.velocity,
                                     pos, engine, routedMidi, &info);
                captureChordOn (info, ppqAt (pos), bpm);
            }
            else
            {
                captureChordOff (router.handleNoteOff (MidiOutputRouter::Source::Voice, e.note, pos, routedMidi),
                                 ppqAt (pos));
            }
            ++voiceIdx;
        }
    }

    //==========================================================================
    // PASO 5 — STRUM: entrada progresiva de las notas (ASC / DESC), por muestra
    //==========================================================================
    strum.process (routedMidi, expandedChordMidi, numSamples);

    //==========================================================================
    // PASO 6 — PIANO INTERNO (OUTPUT MODE = PIANO + MIDI)
    //==========================================================================
    const int outputMode = juce::jlimit (0, 1, juce::roundToInt (pOutputMode->load()));
    const bool pianoActive = outputMode == OutputMode::PianoAndMidi;

    if (pianoActive)
    {
        pianoSynth.render (buffer, expandedChordMidi, 0, numSamples);

        gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));
        if (gainSmoothed.isSmoothing())
        {
            const float startGain = gainSmoothed.getCurrentValue();
            const float endGain = gainSmoothed.skip (numSamples);
            buffer.applyGainRamp (0, numSamples, startGain, endGain);
        }
        else
        {
            buffer.applyGain (gainSmoothed.getTargetValue());
        }
    }
    else if (pianoWasActive)
    {
        pianoSynth.allNotesOff();
    }
    pianoWasActive = pianoActive;

    //==========================================================================
    // PASO 7 — SALIDA MIDI SIEMPRE ACTIVA: el buffer de salida se REEMPLAZA por
    //          los acordes completos (con su STRUM) en el canal elegido
    //==========================================================================
    midiMessages.swapWith (expandedChordMidi);

    // Reloj de la captura
    const double blockEndPpq = blockStartPpq + (double) numSamples * ppqPerSample;
    if (captureBase == CaptureBase::Host)
        lastHostBlockEndPpq = blockEndPpq;
    else if (captureBase == CaptureBase::Free)
        freeClockPpq = blockEndPpq;
    if (captureBase != CaptureBase::None && blockEndPpq - regionStartPpq >= 1.0 / 32.0)
        flushRegion (blockEndPpq);
    capturingNow.store (captureBase != CaptureBase::None);
    captureFromHost.store (captureBase == CaptureBase::Host);

    // Visualizador: sólo las notas que ya suenan (con STRUM se encienden una a una)
    uint64_t lo = 0, hi = 0;
    strum.getSoundingBitmask (lo, hi);
    activeNotesBitmask[0].store (lo, std::memory_order_release);
    activeNotesBitmask[1].store (hi, std::memory_order_release);
    voiceChordSounding.store (router.isVoiceSlotActive());
}

//==============================================================================
juce::AudioProcessorEditor* SmartKeysAudioProcessor::createEditor()
{
    return new SmartKeysAudioProcessorEditor (*this);
}

void SmartKeysAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Guardar el proyecto incluye los acordes recién grabados (sólo el hilo de mensajes vacía el FIFO)
    if (juce::MessageManager::existsAndIsCurrentThread())
        drainCaptureEvents();

    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 3, nullptr);
    state.setProperty ("customMask", (int) customMask.load(), nullptr);
    {
        const juce::ScopedLock sl (hardwareStateLock);
        state.setProperty ("hwDevice", hardwareDeviceName, nullptr);
    }
    state.setProperty ("hwChannel", hardwareChannelMode.load(), nullptr);
    state.setProperty ("quantize", quantizeIndex.load(), nullptr);
    state.setProperty ("captureSync", captureSync.load(), nullptr);
    state.setProperty ("takeMode", takeMode.load(), nullptr);
    state.setProperty ("newChordLength", newChordLengthIndex.load(), nullptr);
    state.setProperty ("showRoman", showRoman.load(), nullptr);
    state.setProperty ("editorScale", (double) editorScale.load(), nullptr);

    if (auto xml = state.createXml())
    {
        {
            const juce::ScopedLock sl (progressionLock);
            xml->addChildElement (progression.toXml().release());
        }
        copyXmlToBinary (*xml, destData);
    }
}

void SmartKeysAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Progresión guardada con el proyecto
    {
        const juce::ScopedLock sl (progressionLock);
        if (auto* p = xml->getChildByName ("Progression"))
            progression.fromXml (*p);
        else
            progression.fromXml (juce::XmlElement ("Progression"));
    }
    xml->deleteAllChildElementsWithTagName ("Progression");

    auto tree = juce::ValueTree::fromXml (*xml);

    // Proyectos de la 1.1: OUTPUT MODE tenía 3 opciones (Piano / MIDI / Both).
    // Ahora el MIDI sale siempre: "MIDI Out" -> MIDI Only, el resto -> Piano + MIDI.
    if ((int) tree.getProperty ("stateVersion", 1) < 3)
    {
        for (auto child : tree)
        {
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == ParamIDs::outputMode)
            {
                const int old = juce::roundToInt ((double) child.getProperty ("value", 2.0));
                child.setProperty ("value", old == 1 ? (float) OutputMode::MidiOnly : (float) OutputMode::PianoAndMidi, nullptr);
            }
        }
        if (tree.hasProperty ("exportQuantize"))
            tree.setProperty ("quantize", (bool) tree.getProperty ("exportQuantize") ? QuantizeGrid::kDefault : 0, nullptr);
    }

    apvts.replaceState (tree);   // dispara parameterChanged -> máscara de la escala

    const int savedMask = (int) tree.getProperty ("customMask", -1);
    if (savedMask > 0 && savedMask < 4096)
        customMask.store ((uint16_t) savedMask);
    else
        customMask.store (ScaleChordEngine::getPitchClassMask ((int) pRootKey->load(), (int) pScale->load()));

    // El dispositivo guardado se abre en el hilo de mensajes (timerCallback)
    const auto savedDevice = tree.getProperty ("hwDevice", juce::String()).toString();
    {
        const juce::ScopedLock sl (hardwareStateLock);
        if (savedDevice != hardwareDeviceName)
            hardwareReopenRequested.store (true);
        hardwareDeviceName = savedDevice;
        hardwareOpenFailed = false;
        hardwareStatus.clear();
    }
    hardwareChannelMode.store (juce::jlimit (0, 2, (int) tree.getProperty ("hwChannel", (int) HardwareMicInput::ChannelMode::Mix)));
    hardwareMic.setChannelMode ((HardwareMicInput::ChannelMode) hardwareChannelMode.load());

    setQuantizeIndex ((int) tree.getProperty ("quantize", QuantizeGrid::kDefault));
    setCaptureSync ((int) tree.getProperty ("captureSync", CaptureSync::PlayOrRecord));
    setTakeMode ((int) tree.getProperty ("takeMode", TakeMode::Replace));
    setNewChordLengthIndex ((int) tree.getProperty ("newChordLength", NewChordLength::kDefault));
    setShowRoman ((bool) tree.getProperty ("showRoman", false));
    setEditorScale ((float) (double) tree.getProperty ("editorScale", 1.0));

    progressionEdited();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartKeysAudioProcessor();
}
