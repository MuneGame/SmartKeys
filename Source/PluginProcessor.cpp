/*
  ==============================================================================

    PluginProcessor.cpp
    SmartKeys (MIWN) — Piano inteligente, generador de acordes y controlador vocal.

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

// Los ParameterID (y sus versiones) se conservan: la automatización y los
// proyectos guardados con versiones anteriores siguen funcionando. (1.5: ya
// no existe "MIDI Out Channel": la salida MIDI va siempre por el canal 1.)
juce::AudioProcessorValueTreeState::ParameterLayout SmartKeysAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

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
        NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -48.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::masterGain, 1 }, "Master Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    // STRUM SPEED: retardo progresivo entre las notas del acorde (0 = todas juntas)
    NormalisableRange<float> strumRange (0.0f, (float) StrumScheduler::kMaxStrumMs, 0.1f);
    strumRange.setSkewForCentre (50.0f);
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::strumMs, 4 }, "Strum Speed", strumRange, 0.0f,
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

// Entrada de micrófono (mono o estéreo; sidechain opcional) + salida estéreo del piano
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
    pRootKey     = apvts.getRawParameterValue (ParamIDs::rootKey);
    pScale       = apvts.getRawParameterValue (ParamIDs::scale);
    pMode        = apvts.getRawParameterValue (ParamIDs::mode);
    pChordExt    = apvts.getRawParameterValue (ParamIDs::chordExt);
    pComplexVar  = apvts.getRawParameterValue (ParamIDs::complexVar);
    pMicOn       = apvts.getRawParameterValue (ParamIDs::micOn);
    pMicGate     = apvts.getRawParameterValue (ParamIDs::micGate);
    pMasterGain  = apvts.getRawParameterValue (ParamIDs::masterGain);
    pStrumMs     = apvts.getRawParameterValue (ParamIDs::strumMs);
    pStrumDir    = apvts.getRawParameterValue (ParamIDs::strumDir);

    apvts.addParameterListener (ParamIDs::rootKey, this);
    apvts.addParameterListener (ParamIDs::scale, this);

    // Las listas de oyentes de JUCE reservan memoria la primera vez que avisan:
    // que eso ocurra aquí y no en la primera automatización del host (hilo de audio)
    for (auto* id : { ParamIDs::rootKey, ParamIDs::scale })
        if (auto* p = apvts.getParameter (id))
            p->sendValueChangedMessageToListeners (p->getValue());

    customMask.store (ScaleChordEngine::getPitchClassMask (0, 0));

    for (auto& a : activeNotesBitmask)
        a.store (0);

    routedMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.ensureSize (kMidiReserveBytes);

    // Vigilancia del micrófono del sistema (hilo de mensajes, 4 veces por segundo)
    startTimer (250);
}

SmartKeysAudioProcessor::~SmartKeysAudioProcessor()
{
    // 1) Ningún temporizador más; 2) el micrófono del sistema se cierra (el
    //    IOProc de CoreAudio termina antes de destruir su búfer circular)
    stopTimer();
    systemMic.shutdown();

    apvts.removeParameterListener (ParamIDs::rootKey, this);
    apvts.removeParameterListener (ParamIDs::scale, this);
}

//==============================================================================
// Hilo de mensajes: abre el micrófono del sistema sólo cuando hace falta
// (MIC VOICE en ON y el DAW no manda voz por la entrada del plugin) y lo cierra
// en cuanto deja de hacer falta. Nunca toca el audio de salida del DAW.
void SmartKeysAudioProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (pluginInputActive.load (std::memory_order_relaxed))
        lastPluginInputActiveMs = now;

    const bool micOn = pMicOn->load() > 0.5f;
    const bool dawSendsVoice = now - lastPluginInputActiveMs < kInputHoldMs;
    systemMic.update (micOn && ! dawSendsVoice);
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
void SmartKeysAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    pianoSynth.prepare (currentSampleRate, samplesPerBlock);
    voiceTracker.prepare (currentSampleRate);

    router.reset();
    strum.prepare (currentSampleRate);
    routedMidi.ensureSize (kMidiReserveBytes);
    routedMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();

    gainSmoothed.reset (currentSampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));

    appliedRoot = appliedScale = appliedMode = appliedExt = appliedVar = -1;
    micWasOn = false;
    trackerSource = -1;
    trackerRate = currentSampleRate;
    inputSilentSamples = 1 << 30;

    updateEngineIfNeeded();
}

void SmartKeysAudioProcessor::releaseResources()
{
    voiceTracker.reset();
    pianoSynth.allNotesOff();
    strum.reset();
}

bool SmartKeysAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Entrada de micrófono: mono o estéreo, o desactivada (es un sidechain;
    // sin ella SmartKeys lee el micrófono del sistema)
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

    if (root != appliedRoot || scale != appliedScale || mask != appliedMask)
        engine.setState (root, scale, mask);   // reconstruye tablas sin asignar memoria

    if (mode != appliedMode)
        engine.setMode (static_cast<OperationMode> (mode));   // 0 Smart, 1 Single, 2 Complex

    if (ext != appliedExt)
        engine.setChordExtension (static_cast<ChordExtension> (ext));

    if (var != appliedVar)
        engine.setComplexVariation (static_cast<ComplexVariation> (var));

    appliedRoot = root;   appliedScale = scale; appliedMask = mask;
    appliedMode = mode;   appliedExt = ext;     appliedVar = var;
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
                router.handleNoteOn (MidiOutputRouter::Source::Screen, e.note, kMidiChannel, (juce::uint8) e.velocity,
                                     0, engine, out);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Screen, e.note, 0, out);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

//==============================================================================
// Hilo de audio: sin memoria dinámica, sin candados, sin llamadas a la
// interfaz ni al MessageManager. Sólo atómicos y FIFOs de tamaño fijo.
void SmartKeysAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Algunos hosts (FL Studio) llaman a process() con 0 muestras para entregar
    // parámetros. No hay ninguna posición válida para un evento MIDI: nada sale.
    if (numSamples <= 0)
    {
        midiMessages.clear();
        return;
    }

    updateEngineIfNeeded();
    voiceTracker.setGateThresholdDb (pMicGate->load());
    strum.setStrum ((double) pStrumMs->load(), juce::roundToInt (pStrumDir->load()) == StrumDir::Descending);

    // Capacidad reservada: tras el primer bloque no hay asignaciones
    routedMidi.clear();
    routedMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);

    //==========================================================================
    // PASO 1 — ELEGIR DE DÓNDE LLEGA LA VOZ (antes de tocar el buffer)
    //   a) La entrada del plugin, si el DAW manda audio por ella (sidechain).
    //   b) Si llega en silencio digital (Channel Rack de FL Studio sin ruteo):
    //      el micrófono del sistema, que escribe su propio hilo de CoreAudio en
    //      un búfer circular lock-free.
    //==========================================================================
    const bool micOn = pMicOn->load() > 0.5f;

    if (! micOn && micWasOn)
        router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, routedMidi);
    if (micOn && ! micWasOn)
        voiceTracker.forceRelease (0);   // al encender, la siguiente nota estable dispara su acorde
    micWasOn = micOn;

    // a) Entrada del plugin: punteros a los canales del bus (sin copiar nada)
    const float* const* inputChannels = nullptr;
    int numInputChannels = 0;
    float inputPeak = 0.0f;

    if (getBusCount (true) > 0)
    {
        if (auto* bus = getBus (true, 0); bus != nullptr && bus->isEnabled())
        {
            const int first = getChannelIndexInProcessBlockBuffer (true, 0, 0);
            numInputChannels = juce::jmin (bus->getNumberOfChannels(), buffer.getNumChannels() - first);
            if (numInputChannels > 0)
            {
                inputChannels = buffer.getArrayOfReadPointers() + first;
                for (int ch = 0; ch < numInputChannels; ++ch)
                    inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (first + ch, 0, numSamples));
            }
            else
            {
                numInputChannels = 0;
            }
        }
    }

    inputSilentSamples = inputPeak > kInputSignalFloor ? 0 : juce::jmin (inputSilentSamples + numSamples, 1 << 30);
    const bool inputActive = inputChannels != nullptr
                             && inputSilentSamples < (int) (currentSampleRate * kInputSilenceSeconds);
    pluginInputActive.store (inputActive, std::memory_order_relaxed);

    // b) Micrófono del sistema (no en una exportación: el disco va más rápido que el micrófono)
    const bool systemMicReady = ! isNonRealtime() && systemMic.isRunning();

    int source = VoiceSource::None;
    if (inputActive)                    source = VoiceSource::PluginInput;
    else if (systemMicReady)            source = VoiceSource::SystemMic;
    else if (inputChannels != nullptr)  source = VoiceSource::PluginInput;   // conectada pero en silencio

    // El detector trabaja a la frecuencia de la fuente: al cambiar de fuente,
    // de frecuencia o de dispositivo se suelta la nota de voz y se reinicia
    // (prepare() no asigna memoria).
    const double sourceRate = source == VoiceSource::SystemMic ? systemMic.getSampleRate() : currentSampleRate;
    const uint32_t micGeneration = systemMic.getGeneration();
    if (source != trackerSource || std::abs (sourceRate - trackerRate) > 0.5
          || (source == VoiceSource::SystemMic && micGeneration != trackerGeneration))
    {
        if (router.isVoiceSlotActive())
            router.handleNoteOff (MidiOutputRouter::Source::Voice, 0, 0, routedMidi);
        voiceTracker.prepare (sourceRate > 1000.0 ? sourceRate : currentSampleRate);
        if (source == VoiceSource::SystemMic)
            systemMic.discardAll();   // sólo audio nuevo y continuo
        trackerSource = source;
        trackerRate = sourceRate;
        trackerGeneration = micGeneration;
    }

    int numVoiceEvents = 0;
    int voiceSamples = numSamples;   // muestras analizadas (en la frecuencia de la fuente)

    if (source == VoiceSource::PluginInput)
    {
        numVoiceEvents = voiceTracker.process (inputChannels, numInputChannels, numSamples, engine);
    }
    else if (source == VoiceSource::SystemMic)
    {
        // Todo lo que llegó desde el bloque anterior; si el DAW se detuvo un
        // rato, sólo lo más reciente (latencia acotada).
        const double expected = (double) numSamples * sourceRate / currentSampleRate;
        const int maxBacklog = juce::jlimit (1024, smartkeys::SystemMicInput::kRingCapacity - 1, (int) expected + 8192);
        voiceSamples = systemMic.pull (systemMicScratch.data(), (int) systemMicScratch.size(), maxBacklog);
        const float* channels[1] = { systemMicScratch.data() };
        numVoiceEvents = voiceSamples > 0 ? voiceTracker.process (channels, 1, voiceSamples, engine) : 0;
    }

    if (source != VoiceSource::SystemMic)
        systemMic.discardAll();   // nunca se acumula audio viejo del micrófono

    //==========================================================================
    // PASO 2 — MEDIDOR + NOTA DETECTADA EN VIVO (atómicos que lee la interfaz)
    //==========================================================================
    voiceSource.store (source);
    micLevelDb.store (source != VoiceSource::None ? voiceTracker.inputLevelDb.load() : -100.0f);

    //==========================================================================
    // PASO 3 — SILENCIAR LA VOZ CRUDA (ya analizada) ANTES DE RENDERIZAR EL PIANO
    //==========================================================================
    buffer.clear();

    //==========================================================================
    // PASO 4 — EXPANDIR CADA NOTA DE ENTRADA AL ACORDE COMPLETO
    //          (MIDI / Piano Roll, teclado en pantalla / PC y voz, en orden de muestra)
    //==========================================================================
    drainScreenNotes (routedMidi);

    const int routedVoiceEvents = micOn ? numVoiceEvents : 0;
    const int lastSample = numSamples - 1;
    auto voicePosition = [&] (int index)
    {
        // Los eventos del micrófono del sistema se reparten a lo largo del bloque
        const int offset = voiceTracker.getEvent (index).sampleOffset;
        const int pos = voiceSamples == numSamples ? offset
                                                   : (int) ((juce::int64) offset * numSamples / juce::jmax (1, voiceSamples));
        return juce::jlimit (0, lastSample, pos);
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
            // Bytes crudos: nunca se construye un juce::MidiMessage (un SysEx asignaría memoria)
            const auto meta = *midiIt;
            ++midiIt;
            const auto* data = meta.data;
            const int size = meta.numBytes;
            if (data == nullptr || size <= 0)
                continue;

            const int pos = juce::jlimit (0, lastSample, meta.samplePosition);
            const auto status = (uint8_t) data[0];
            const int type = status & 0xF0;
            const int note = size >= 2 ? (data[1] & 0x7F) : 0;
            const int velocity = size >= 3 ? (data[2] & 0x7F) : 0;

            if (type == 0x90 && size >= 3 && velocity > 0)
            {
                router.handleNoteOn (MidiOutputRouter::Source::Midi, note, kMidiChannel,
                                     (juce::uint8) velocity, pos, engine, routedMidi);
            }
            else if ((type == 0x80 || type == 0x90) && size >= 3)
            {
                router.handleNoteOff (MidiOutputRouter::Source::Midi, note, pos, routedMidi);
            }
            else if (status >= 0x80 && status < 0xF0 && size <= 3)
            {
                // CC, sustain, pitch bend, aftertouch... al canal 1
                if (type == 0xB0 && (note == 120 || note == 123))
                    router.releaseAll (pos, routedMidi);   // All Sound Off / All Notes Off del DAW

                juce::uint8 remapped[3] = { (juce::uint8) (type | (kMidiChannel - 1)),
                                            (juce::uint8) (size >= 2 ? data[1] : 0),
                                            (juce::uint8) (size >= 3 ? data[2] : 0) };
                routedMidi.addEvent (remapped, size, pos);
            }
            else
            {
                routedMidi.addEvent (data, size, pos);   // mensajes de sistema: tal cual
            }
        }
        else
        {
            const auto& e = voiceTracker.getEvent (voiceIdx);
            const int pos = voicePosition (voiceIdx);

            if (e.isNoteOn)
                router.handleNoteOn (MidiOutputRouter::Source::Voice, e.note, kMidiChannel, (juce::uint8) e.velocity,
                                     pos, engine, routedMidi);
            else
                router.handleNoteOff (MidiOutputRouter::Source::Voice, e.note, pos, routedMidi);
            ++voiceIdx;
        }
    }

    //==========================================================================
    // PASO 5 — STRUM: entrada progresiva de las notas (ASC / DESC), por muestra
    //==========================================================================
    strum.process (routedMidi, expandedChordMidi, numSamples);

    //==========================================================================
    // PASO 6 — PIANO INTERNO (siempre; el volumen se ajusta con GAIN)
    //==========================================================================
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

    //==========================================================================
    // PASO 7 — SALIDA MIDI SIEMPRE ACTIVA: el buffer de salida se REEMPLAZA por
    //          los acordes completos (con su STRUM) en el canal 1
    //==========================================================================
    midiMessages.swapWith (expandedChordMidi);

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
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 5, nullptr);
    state.setProperty ("customMask", (int) customMask.load(), nullptr);
    state.setProperty ("editorScale", (double) editorScale.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SmartKeysAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Proyectos de la 1.1–1.3: la progresión guardada ya no existe
    xml->deleteAllChildElementsWithTagName ("Progression");

    auto tree = juce::ValueTree::fromXml (*xml);

    // Parámetros y ajustes que ya no existen (afinación A4, microafinación,
    // OUTPUT MODE, puerto MIDI, captura de progresiones, micrófono DEVICE de la
    // 1.1, canal MIDI de la 1.4): se descartan para que no vuelvan a guardarse
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto child = tree.getChild (i);
        if (child.hasType ("PARAM") && apvts.getParameter (child.getProperty ("id").toString()) == nullptr)
            tree.removeChild (i, nullptr);
    }
    for (auto* obsolete : { "virtualPort", "quantize", "exportQuantize", "captureSync", "takeMode",
                            "newChordLength", "showRoman", "hwDevice", "hwChannel" })
        tree.removeProperty (obsolete, nullptr);

    apvts.replaceState (tree);   // dispara parameterChanged -> máscara de la escala

    const int savedMask = (int) tree.getProperty ("customMask", -1);
    if (savedMask > 0 && savedMask < 4096)
        customMask.store ((uint16_t) savedMask);
    else
        customMask.store (ScaleChordEngine::getPitchClassMask ((int) pRootKey->load(), (int) pScale->load()));

    setEditorScale ((float) (double) tree.getProperty ("editorScale", 1.0));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartKeysAudioProcessor();
}
