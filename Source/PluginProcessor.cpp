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

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::micGate, 1 }, "Mic Gate",
        NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -40.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::pianoOn, 1 }, "Internal Piano", true));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::masterGain, 1 }, "Master Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::microTuning, 1 }, "Scale Microtuning", false));

    return { params.begin(), params.end() };
}

//==============================================================================
SmartKeysAudioProcessor::SmartKeysAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Mic / Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",          juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SmartKeysState", createParameterLayout())
{
    pRefPitch    = apvts.getRawParameterValue (ParamIDs::refPitch);
    pRootKey     = apvts.getRawParameterValue (ParamIDs::rootKey);
    pScale       = apvts.getRawParameterValue (ParamIDs::scale);
    pMode        = apvts.getRawParameterValue (ParamIDs::mode);
    pChordExt    = apvts.getRawParameterValue (ParamIDs::chordExt);
    pComplexVar  = apvts.getRawParameterValue (ParamIDs::complexVar);
    pMicOn       = apvts.getRawParameterValue (ParamIDs::micOn);
    pMicGate     = apvts.getRawParameterValue (ParamIDs::micGate);
    pPianoOn     = apvts.getRawParameterValue (ParamIDs::pianoOn);
    pMasterGain  = apvts.getRawParameterValue (ParamIDs::masterGain);
    pMicroTuning = apvts.getRawParameterValue (ParamIDs::microTuning);

    apvts.addParameterListener (ParamIDs::rootKey, this);
    apvts.addParameterListener (ParamIDs::scale, this);

    customMask.store (ScaleChordEngine::getPitchClassMask (0, 0));

    for (auto& a : activeNotesBitmask)
        a.store (0);

    outMidi.ensureSize (kMidiReserveBytes);
}

SmartKeysAudioProcessor::~SmartKeysAudioProcessor()
{
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
    if (scope.blockSize1 > 0)
        screenEvents[(size_t) scope.startIndex1] = { juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity), isNoteOn };
    else if (scope.blockSize2 > 0)
        screenEvents[(size_t) scope.startIndex2] = { juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity), isNoteOn };
}

//==============================================================================
void SmartKeysAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pianoSynth.prepare (sampleRate, samplesPerBlock);
    voiceTracker.prepare (sampleRate);
    router.reset();

    outMidi.ensureSize (kMidiReserveBytes);
    outMidi.clear();

    gainSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));

    appliedRoot = appliedScale = appliedMode = appliedExt = appliedVar = -1;
    appliedRef = 0.0f;
    micWasOn = false;
    pianoWasOn = pPianoOn->load() > 0.5f;

    updateEngineIfNeeded();
}

void SmartKeysAudioProcessor::releaseResources()
{
    voiceTracker.reset();
    pianoSynth.allNotesOff();
}

bool SmartKeysAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    if (layouts.inputBuses.size() > 0)
    {
        const auto in = layouts.getChannelSet (true, 0);
        if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
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

void SmartKeysAudioProcessor::drainScreenNotes (juce::MidiBuffer& out)
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
                router.handleNoteOn (MidiOutputRouter::Source::Screen, e.note, 1, (juce::uint8) e.velocity, 0, engine, out);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Screen, e.note, 0, out);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

//==============================================================================
void SmartKeysAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    updateEngineIfNeeded();
    voiceTracker.setGateThresholdDb (pMicGate->load());

    // Capacidad reservada: tras el primer bloque no hay asignaciones
    outMidi.clear();
    outMidi.ensureSize (kMidiReserveBytes);

    //--------------------------------------------------------------------------
    // 1. Controlador vocal: leer el bus de micrófono ANTES de limpiar el buffer
    const bool micOn = pMicOn->load() > 0.5f;
    int numVoiceEvents = 0;

    if (micOn)
    {
        if (getBusCount (true) > 0 && getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled())
        {
            auto micBus = getBusBuffer (buffer, true, 0);
            if (micBus.getNumChannels() > 0)
                numVoiceEvents = voiceTracker.process (micBus.getArrayOfReadPointers(),
                                                       micBus.getNumChannels(), numSamples, engine);
        }
    }
    else if (micWasOn)
    {
        numVoiceEvents = voiceTracker.forceRelease (0);
    }

    //--------------------------------------------------------------------------
    // 2. Bloqueo total del audio crudo del micrófono: la salida sólo será el piano
    buffer.clear();

    //--------------------------------------------------------------------------
    // 3. Router MIDI: pánico, teclado en pantalla, MIDI entrante y voz (ordenados por muestra)
    if (panicRequested.exchange (false))
        router.releaseAll (0, outMidi);

    drainScreenNotes (outMidi);

    auto midiIt  = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    int voiceIdx = 0;

    while (midiIt != midiEnd || voiceIdx < numVoiceEvents)
    {
        const bool takeMidi = (midiIt != midiEnd)
                              && (voiceIdx >= numVoiceEvents
                                  || (*midiIt).samplePosition <= voiceTracker.getEvent (voiceIdx).sampleOffset);

        if (takeMidi)
        {
            const auto meta = *midiIt;
            const auto msg = meta.getMessage();
            const int pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);

            if (msg.isNoteOn())
                router.handleNoteOn (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), msg.getChannel(),
                                     msg.getVelocity(), pos, engine, outMidi);
            else if (msg.isNoteOff())
                router.handleNoteOff (MidiOutputRouter::Source::Midi, msg.getNoteNumber(), pos, outMidi);
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    router.releaseAll (pos, outMidi);
                outMidi.addEvent (msg, pos);   // CC, sustain, pitch bend, etc. pasan intactos
            }
            ++midiIt;
        }
        else
        {
            const auto& e = voiceTracker.getEvent (voiceIdx);
            const int pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), e.sampleOffset);

            if (e.isNoteOn)
                router.handleNoteOn (MidiOutputRouter::Source::Voice, e.note, 1, (juce::uint8) e.velocity,
                                     pos, engine, outMidi);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Voice, e.note, pos, outMidi);
            ++voiceIdx;
        }
    }

    if (! micOn && micWasOn)
        voiceTracker.reset();
    micWasOn = micOn;

    //--------------------------------------------------------------------------
    // 4. Piano interno
    const bool pianoOn = pPianoOn->load() > 0.5f;
    if (pianoOn)
    {
        pianoSynth.render (buffer, outMidi, 0, numSamples);

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
    else if (pianoWasOn)
    {
        pianoSynth.allNotesOff();
    }
    pianoWasOn = pianoOn;

    //--------------------------------------------------------------------------
    // 5. MIDI de salida hacia el Piano Roll del DAW (acordes completos)
    midiMessages.swapWith (outMidi);

    //--------------------------------------------------------------------------
    // 6. Visualizador
    uint64_t lo = 0, hi = 0;
    router.getActiveBitmask (lo, hi);
    activeNotesBitmask[0].store (lo, std::memory_order_release);
    activeNotesBitmask[1].store (hi, std::memory_order_release);
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
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartKeysAudioProcessor();
}
