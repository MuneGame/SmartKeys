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

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::outputMode, 3 }, "Output Mode",
        StringArray { "Internal Piano", "External Plugin (MIDI Out)", "Both" }, OutputMode::Both));

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

    apvts.addParameterListener (ParamIDs::rootKey, this);
    apvts.addParameterListener (ParamIDs::scale, this);

    customMask.store (ScaleChordEngine::getPitchClassMask (0, 0));

    for (auto& a : activeNotesBitmask)
        a.store (0);

    expandedChordMidi.ensureSize (kMidiReserveBytes);
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

//==============================================================================
// Hilo de mensajes: grabador de acordes + gestión del micrófono de hardware
void SmartKeysAudioProcessor::timerCallback()
{
    captureFifo.drain ([this] (const CapturedNoteEvent& e) { recorder.add (e); });
    openHardwareMicIfNeeded();
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
void SmartKeysAudioProcessor::clearCapturedChords()
{
    captureFifo.drain ([] (const CapturedNoteEvent&) {});
    recorder.clear();
}

juce::File SmartKeysAudioProcessor::exportCapturedChords (const juce::String& baseName)
{
    captureFifo.drain ([this] (const CapturedNoteEvent& e) { recorder.add (e); });
    return recorder.writeMidiFile (baseName, hostBpm.load(), exportQuantize.load());
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
    expandedChordMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();

    gainSmoothed.reset (currentSampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));

    appliedRoot = appliedScale = appliedMode = appliedExt = appliedVar = -1;
    appliedRef = 0.0f;
    micWasOn = false;
    lastVoiceSource = -1;
    pianoWasActive = juce::roundToInt (pOutputMode->load()) != OutputMode::ExternalMidi;

    updateEngineIfNeeded();
}

void SmartKeysAudioProcessor::releaseResources()
{
    dawTracker.reset();
    hardwareTracker.reset();
    pianoSynth.allNotesOff();
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

void SmartKeysAudioProcessor::drainScreenNotes (juce::MidiBuffer& out, int outChannel)
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
                router.handleNoteOn (MidiOutputRouter::Source::Screen, e.note, outChannel, (juce::uint8) e.velocity, 0, engine, out);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Screen, e.note, 0, out);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

// Publica cada NoteOn/NoteOff de salida en el FIFO del grabador (sin asignar memoria)
void SmartKeysAudioProcessor::captureOutputNotes (const juce::MidiBuffer& notes, double sampleRate,
                                                  double bpm, int numSamples)
{
    const double qnPerSample = bpm / (60.0 * juce::jmax (1.0, sampleRate));

    for (const auto meta : notes)
    {
        if (meta.numBytes < 3)
            continue;

        const int status = meta.data[0] & 0xF0;
        const int velocity = meta.data[2] & 0x7F;
        const bool on  = status == 0x90 && velocity > 0;
        const bool off = status == 0x80 || (status == 0x90 && velocity == 0);
        if (! on && ! off)
            continue;

        CapturedNoteEvent e;
        e.timeQn = captureClockQn + (double) meta.samplePosition * qnPerSample;
        e.note = meta.data[1] & 0x7F;
        e.velocity = on ? velocity : 0;
        e.isNoteOn = on;
        captureFifo.push (e);
    }

    captureClockQn += (double) numSamples * qnPerSample;
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

    // Capacidad reservada: tras el primer bloque no hay asignaciones
    expandedChordMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);

    // Tempo del proyecto (para el grabador de acordes)
    double bpm = hostBpm.load();
    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
            if (auto hostTempo = position->getBpm())
                if (*hostTempo > 20.0 && *hostTempo < 400.0)
                    bpm = *hostTempo;
    hostBpm.store (bpm);

    //==========================================================================
    // PASO 1 — LEER EL MICRÓFONO Y DETECTAR LA VOZ (antes de tocar el buffer)
    //==========================================================================
    const bool micOn  = pMicOn->load() > 0.5f;
    const int  source = juce::jlimit (0, 1, juce::roundToInt (pMicSource->load()));

    if (source != lastVoiceSource)
    {
        // Cambio de fuente: soltar el acorde vocal que estuviera sonando
        router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, expandedChordMidi);
        dawTracker.forceRelease (0);
        hardwareTracker.forceRelease (0);
        lastVoiceSource = source;
    }

    if (! micOn && micWasOn)
        router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, expandedChordMidi);
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
    //          (MIDI, teclado en pantalla / PC, Piano Roll y voz, en orden de muestra)
    //==========================================================================
    const int outChannel = 1 + juce::jlimit (0, 15, juce::roundToInt (pMidiOutCh->load()));

    if (panicRequested.exchange (false))
        router.releaseAll (0, expandedChordMidi);

    drainScreenNotes (expandedChordMidi, outChannel);

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
                router.handleNoteOn (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), outChannel,
                                     msg.getVelocity(), pos, engine, expandedChordMidi);
            else if (msg.isNoteOff())
                router.handleNoteOff (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), pos, expandedChordMidi);
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    router.releaseAll (pos, expandedChordMidi);

                if (msg.getChannel() > 0)
                {
                    auto routed = msg;              // CC, sustain, pitch bend... al canal MIDI elegido
                    routed.setChannel (outChannel);
                    expandedChordMidi.addEvent (routed, pos);
                }
                else
                {
                    expandedChordMidi.addEvent (msg, pos);
                }
            }
            ++midiIt;
        }
        else
        {
            const auto& e = tracker->getEvent (voiceIdx);
            const int pos = voicePosition (voiceIdx);

            if (e.isNoteOn)
                router.handleNoteOn (MidiOutputRouter::Source::Voice, e.note, outChannel, (juce::uint8) e.velocity,
                                     pos, engine, expandedChordMidi);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Voice, e.note, pos, expandedChordMidi);
            ++voiceIdx;
        }
    }

    //==========================================================================
    // PASO 5 — PIANO INTERNO (OUTPUT MODE = INTERNAL PIANO o BOTH)
    //==========================================================================
    const int outputMode = juce::jlimit (0, 2, juce::roundToInt (pOutputMode->load()));
    const bool pianoActive = outputMode != OutputMode::ExternalMidi;

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
    // PASO 6 — GRABADOR DE ACORDES (para "DRAG CHORDS TO PIANO ROLL")
    //==========================================================================
    captureOutputNotes (expandedChordMidi, currentSampleRate, bpm, numSamples);

    //==========================================================================
    // PASO 7 — SALIDA MIDI: el buffer de salida se REEMPLAZA por los acordes
    //          completos (EXTERNAL PLUGIN o BOTH); en INTERNAL PIANO no sale MIDI.
    //==========================================================================
    if (outputMode == OutputMode::InternalPiano)
        midiMessages.clear();
    else
        midiMessages.swapWith (expandedChordMidi);

    // Visualizador
    uint64_t lo = 0, hi = 0;
    router.getActiveBitmask (lo, hi);
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
    auto state = apvts.copyState();
    state.setProperty ("customMask", (int) customMask.load(), nullptr);
    {
        const juce::ScopedLock sl (hardwareStateLock);
        state.setProperty ("hwDevice", hardwareDeviceName, nullptr);
    }
    state.setProperty ("hwChannel", hardwareChannelMode.load(), nullptr);
    state.setProperty ("exportQuantize", exportQuantize.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SmartKeysAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
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
            exportQuantize.store ((bool) tree.getProperty ("exportQuantize", true));
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartKeysAudioProcessor();
}
