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
    portRoutedMidi.ensureSize (kMidiReserveBytes);
    portExpandedMidi.ensureSize (kMidiReserveBytes);

    chordPortEnabled.store (isChordPortEnabledByDefault (wrapperType));

    // Vigilancia del micrófono del sistema y del teclado virtual (hilo de mensajes, 4 veces por segundo)
    startTimer (250);
}

SmartKeysAudioProcessor::~SmartKeysAudioProcessor()
{
    // 1) Ningún temporizador más; 2) el teclado virtual apaga lo que esta
    //    instancia dejó sonando y la suelta; 3) el micrófono del sistema se
    //    cierra (el IOProc de CoreAudio termina antes de destruir su búfer)
    stopTimer();
    disconnectChordPort();
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

    updateChordPort();
}

//==============================================================================
// Teclado MIDI virtual "SmartKeys Chords": se crea en el hilo de mensajes en
// cuanto el plugin procesa audio de verdad (nunca durante el escaneo de
// plugins). Uno solo por proceso; si el nombre está en uso, se reintenta.
bool SmartKeysAudioProcessor::isChordPortEnabledByDefault (juce::AudioProcessor::WrapperType type)
{
    // SMARTKEYS_CHORD_PORT=1 / 0 lo fuerza (pruebas automáticas)
    const auto forced = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_CHORD_PORT", {});
    if (forced == "1" || forced == "0")
        return forced == "1";

    // FL Studio sólo usa un teclado MIDI si lo activas en MIDI Settings y lo
    // enganchas a un canal; la app, si lo eliges en sus ajustes. Otros DAW meten
    // cualquier teclado nuevo en la grabación de la pista: ahí, apagado.
    if (type == juce::AudioProcessor::wrapperType_Standalone || juce::PluginHostType().isFruityLoops())
        return true;

    // FL Studio para Mac es ".../FL Studio 20xx.app/Contents/MacOS/OsxFL" (y su puente, ilbridge)
    const auto host = juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFullPathName();
    return host.containsIgnoreCase ("FL Studio") || host.containsIgnoreCase ("OsxFL") || host.containsIgnoreCase ("ilbridge");
}

void SmartKeysAudioProcessor::setChordPortEnabled (bool shouldBeEnabled)
{
    if (chordPortEnabled.exchange (shouldBeEnabled) == shouldBeEnabled)
        return;

    updateChordPort();   // conecta, o apaga lo que sonaba y suelta el teclado
}

void SmartKeysAudioProcessor::updateChordPort()
{
   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    if (! chordPortEnabled.load())
    {
        if (chordPort != nullptr)
            disconnectChordPort();
        chordPortState.store (ChordPortState::Off);
        chordPortLooping.store (false, std::memory_order_relaxed);
        return;
    }

    if (! pluginInUse.load())
    {
        chordPortState.store (ChordPortState::Waiting);
        return;
    }

    if (chordPort == nullptr)
    {
        smartkeys::ChordMidiPort::setStandaloneProcess (wrapperType == wrapperType_Standalone);
        chordPort = std::make_unique<juce::SharedResourcePointer<smartkeys::ChordMidiPort>>();
        portRetryTicks = 0;
    }

    if (! portFeedActive.load())
    {
        auto& port = **chordPort;
        const bool available = port.isAvailable() || (++portRetryTicks % 12 == 0 && port.retryOpen());   // cada 3 s
        if (available)
        {
            port.addFeed (portFeed);
            portFeedActive.store (true, std::memory_order_release);
        }
    }

    chordPortState.store (portFeedActive.load() ? ChordPortState::Active : ChordPortState::Unavailable);
   #else
    chordPortState.store (ChordPortState::Unsupported);
   #endif
}

void SmartKeysAudioProcessor::disconnectChordPort()
{
    portFeedActive.store (false, std::memory_order_release);
    if (chordPort != nullptr)
    {
        (*chordPort)->removeFeed (portFeed);   // envía los NoteOff de lo que quedara sonando
        chordPort.reset();                     // la última instancia cierra el teclado
    }
}

void SmartKeysAudioProcessor::resetEchoState() noexcept
{
    // Lo que el teclado ya envió no espera eco
    for (int n = 0; n < 128; ++n)
    {
        echoOnMatched[(size_t) n] = echoOnAccounted[(size_t) n] = portFeed.getNoteOnSerial (n);
        echoOffMatched[(size_t) n] = portFeed.getNoteOffSerial (n);
    }

    echoSuppressed.fill (0);
    midiKeyDown.fill (false);
    suspectTrigger.fill (false);
    echoedStreak = unechoedStreak = 0;
    loopConfirmedUntilMs = noLoopUntilMs = -1.0e12;
    chordPortLooping.store (false, std::memory_order_relaxed);
    portClockValid = false;
}

//==============================================================================
// ECO DEL TECLADO VIRTUAL (hilo de audio, sin memoria dinámica)
//
// Si el DAW devuelve "SmartKeys Chords" a SmartKeys (en FL Studio: el teclado
// activado sin engancharlo a otro instrumento, con SmartKeys seleccionado),
// cada nota de cada acorde volvería a entrar como una tecla y dispararía otro
// acorde. Cómo se distingue un eco de una tecla de verdad:
//
//  1. Candidato: hay un NoteOn de esa nota enviado por el teclado que todavía
//     no volvió (cada envío vuelve UNA vez) y se envió hace muy poco (unos
//     pocos bloques). Si no, es una tecla de verdad: repetir rápido una tecla
//     después de que volviera su eco nunca se confunde.
//  2. Lo aprendido de los envíos anteriores (al empezar cada bloque se revisan
//     los envíos cuyo plazo terminó):
//       - Bucle confirmado (3 ecos seguidos, o una tecla que SIGUE pulsada y
//         "se vuelve a pulsar", imposible en un teclado): candidato = eco.
//       - Sin bucle (2 envíos seguidos que no volvieron; lo normal en FL con
//         el teclado enganchado a FLEX/Serum): candidato = tecla de verdad,
//         salvo que lleguen varios en el mismo bloque (un acorde que vuelve).
//       - Sin datos recientes: por prudencia, candidato = eco.
//  3. El NoteOff de un eco ignorado también se ignora (con su propio crédito);
//     si no llega a tiempo, se deja de esperar.
//  4. Si el bucle aparece de repente, las teclas que se tomaron por reales
//     siendo ecos se sueltan al confirmarlo: nunca quedan notas colgadas.
//==============================================================================
double SmartKeysAudioProcessor::echoWindowMs (double nowMs) const noexcept
{
    return nowMs < loopConfirmedUntilMs ? kLoopEchoWindowMs
                                        : juce::jlimit (100.0, 400.0, 4.0 * maxBlockMs + 50.0);
}

double SmartKeysAudioProcessor::echoEarlyMs() const noexcept
{
    return maxBlockMs + 10.0;   // los relojes del bloque y del envío difieren en hasta ~1 bloque
}

bool SmartKeysAudioProcessor::isPortEchoCandidate (int note, double arrivalMs) const noexcept
{
    const auto i = (size_t) note;
    const uint32_t sentOn = portFeed.getNoteOnSerial (note);
    if (sentOn == echoOnMatched[i])
        return false;   // nada enviado pendiente de volver

    const double since = arrivalMs - portFeed.getLastNoteOnSentMs (note);
    return since >= -echoEarlyMs() && since <= echoWindowMs (arrivalMs);
}

void SmartKeysAudioProcessor::releaseSuspectTriggers() noexcept
{
    for (int n = 0; n < 128; ++n)
    {
        if (suspectTrigger[(size_t) n])
        {
            suspectTrigger[(size_t) n] = false;
            midiKeyDown[(size_t) n] = false;
            routeNoteOff (MidiOutputRouter::Source::Midi, n, 0);
        }
    }
}

void SmartKeysAudioProcessor::updateEchoEvidence (double nowMs) noexcept
{
    const bool wasLooping = nowMs < loopConfirmedUntilMs;
    const double expiry = echoWindowMs (nowMs) + echoEarlyMs();
    int echoed = 0, unechoed = 0;

    for (int n = 0; n < 128; ++n)
    {
        const auto i = (size_t) n;

        const uint32_t sentOn = portFeed.getNoteOnSerial (n);
        if (sentOn != echoOnAccounted[i] && nowMs - portFeed.getLastNoteOnSentMs (n) > expiry)
        {
            if (echoOnMatched[i] == sentOn) ++echoed;
            else                            ++unechoed;
            echoOnAccounted[i] = echoOnMatched[i] = sentOn;
        }

        const uint32_t sentOff = portFeed.getNoteOffSerial (n);
        if (sentOff != echoOffMatched[i] && nowMs - portFeed.getLastNoteOffSentMs (n) > expiry)
        {
            echoOffMatched[i] = sentOff;
            echoSuppressed[i] = 0;   // su NoteOff no volvió: ya no hay eco que esperar
        }
    }

    if (unechoed > 0)
    {
        echoedStreak = 0;
        unechoedStreak += unechoed;
    }
    else if (echoed > 0)
    {
        unechoedStreak = 0;
        echoedStreak += echoed;
    }

    if (unechoed > 0 && unechoedStreak >= 2)
    {
        // Lo que sale no vuelve: no hay bucle
        noLoopUntilMs = nowMs + kNoLoopMemoryMs;
        loopConfirmedUntilMs = -1.0e12;
        suspectTrigger.fill (false);   // eran teclas de verdad
    }
    else if (echoed > 0 && echoedStreak >= 3)
    {
        loopConfirmedUntilMs = nowMs + kLoopMemoryMs;
        noLoopUntilMs = -1.0e12;
    }

    const bool looping = nowMs < loopConfirmedUntilMs;
    chordPortLooping.store (looping, std::memory_order_relaxed);

    if (! looping)
        return;

    // Bucle confirmado: las teclas tomadas por reales siendo ecos se sueltan al
    // confirmarlo y, después, en cuanto no queda ninguna tecla de verdad pulsada
    bool anySuspect = false, anyRealKey = false;
    for (int n = 0; n < 128; ++n)
    {
        anySuspect |= suspectTrigger[(size_t) n];
        anyRealKey |= midiKeyDown[(size_t) n] && ! suspectTrigger[(size_t) n];
    }

    if (anySuspect && (! wasLooping || ! anyRealKey))
        releaseSuspectTriggers();
}

// ¿Este NoteOn de la entrada MIDI es el eco de una nota del teclado virtual?
bool SmartKeysAudioProcessor::isPortEchoNoteOn (int note, double arrivalMs) noexcept
{
    const auto i = (size_t) note;
    if (! isPortEchoCandidate (note, arrivalMs))
    {
        suspectTrigger[i] = false;
        return false;
    }

    ++echoOnMatched[i];   // un envío, un eco

    if (midiKeyDown[i])
    {
        // La tecla sigue pulsada y "se vuelve a pulsar": sólo puede ser el eco
        loopConfirmedUntilMs = arrivalMs + kLoopMemoryMs;
        noLoopUntilMs = -1.0e12;
        echoedStreak = juce::jmax (echoedStreak, 3);
        unechoedStreak = 0;
        return true;
    }

    if (arrivalMs < loopConfirmedUntilMs || echoGroupInBlock)
        return true;

    if (arrivalMs < noLoopUntilMs)
    {
        suspectTrigger[i] = true;   // casi seguro una tecla de verdad (sin bucle)
        return false;
    }

    return true;   // sin datos recientes: prudencia
}

// El NoteOff de un eco ignorado llega poco después de que el teclado enviara su NoteOff
bool SmartKeysAudioProcessor::isPortEchoNoteOff (int note, double arrivalMs) noexcept
{
    const auto i = (size_t) note;
    if (echoSuppressed[i] == 0)
        return false;

    const uint32_t sentOff = portFeed.getNoteOffSerial (note);
    if (sentOff == echoOffMatched[i])
        return false;

    const double since = arrivalMs - portFeed.getLastNoteOffSentMs (note);
    if (since < -echoEarlyMs() || since > echoWindowMs (arrivalMs))
        return false;

    ++echoOffMatched[i];
    --echoSuppressed[i];
    return true;
}

// Hilo de audio: copia las notas de la salida final (con su STRUM) al FIFO del
// teclado virtual, con su instante exacto (un bloque de margen)
void SmartKeysAudioProcessor::feedChordPort (const juce::MidiBuffer& out, int numSamples, double blockStartMs) noexcept
{
    const double msPerSample = 1000.0 / juce::jmax (1.0, currentSampleRate);
    const double base = blockStartMs + (double) numSamples * msPerSample;

    for (const auto meta : out)
    {
        const auto* data = meta.data;
        if (meta.numBytes != 3 || data == nullptr)
            continue;

        const auto type = (uint8_t) (data[0] & 0xF0);
        if (type != 0x80 && type != 0x90)
            continue;   // sólo las notas de los acordes (los CC podrían rebotar en bucle)

        smartkeys::ChordPortEvent e;
        e.dueMs = base + (double) meta.samplePosition * msPerSample;
        e.status = data[0];
        e.data1 = data[1];
        e.data2 = data[2];
        portFeed.push (e);
    }
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

    portRouter.reset();
    portStrum.prepare (currentSampleRate);
    portRoutedMidi.ensureSize (kMidiReserveBytes);
    portRoutedMidi.clear();
    portExpandedMidi.ensureSize (kMidiReserveBytes);
    portExpandedMidi.clear();
    recordingTake = false;
    hostRecording.store (false);

    gainSmoothed.reset (currentSampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterGain->load()));

    appliedRoot = appliedScale = appliedMode = appliedExt = appliedVar = -1;
    micWasOn = false;
    trackerSource = -1;
    trackerRate = currentSampleRate;
    inputSilentSamples = 1 << 30;

    // Teclado virtual: lo que quedara sonando se apaga (el router empieza de cero)
    maxBlockMs = juce::jmax (5.0, 1000.0 * (double) juce::jmax (1, samplesPerBlock) / currentSampleRate);
    resetEchoState();
    portFeed.requestReleaseAll();
    bypassActive = false;

    updateEngineIfNeeded();
}

void SmartKeysAudioProcessor::releaseResources()
{
    voiceTracker.reset();
    pianoSynth.allNotesOff();
    strum.reset();
    portStrum.reset();
    portFeed.requestReleaseAll();
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

//==============================================================================
// Hilo de audio. Cada tecla va a los dos caminos: la salida MIDI + el piano
// (router) y el teclado "SmartKeys Chords" (portRouter), que puede saltársela.
void SmartKeysAudioProcessor::routeNoteOn (MidiOutputRouter::Source source, int trigger, juce::uint8 velocity,
                                           int pos, bool toPort)
{
    router.handleNoteOn (source, trigger, kMidiChannel, velocity, pos, engine, routedMidi);

    if (toPort)
        portRouter.handleNoteOn (source, trigger, kMidiChannel, velocity, pos, engine, portRoutedMidi);
    else
        portRouter.handleNoteOff (source, trigger, pos, portRoutedMidi);   // como el router: lo anterior de esa tecla se suelta
}

void SmartKeysAudioProcessor::routeNoteOff (MidiOutputRouter::Source source, int trigger, int pos)
{
    router.handleNoteOff (source, trigger, pos, routedMidi);
    portRouter.handleNoteOff (source, trigger, pos, portRoutedMidi);
}

void SmartKeysAudioProcessor::routeReleaseAll (int pos)
{
    router.releaseAll (pos, routedMidi);
    portRouter.releaseAll (pos, portRoutedMidi);
}

// Grabación en bucle: FL vuelve a tocar en cada vuelta lo que ya grabó en el
// patrón de SmartKeys; si esos acordes salieran otra vez por el teclado, FL los
// grabaría encima de los de la vuelta anterior (notas dobles). Mientras el DAW
// graba, una tecla que vuelve a sonar en la misma posición (±1 semicorchea) que en
// una vuelta ANTERIOR de esta toma no sale por el teclado; lo nuevo sí. Sin grabar
// no se filtra nada: al tocar en vivo sobre un bucle, cada vuelta sale entera.
bool SmartKeysAudioProcessor::isLoopRepeat (int note, double ppq) noexcept
{
    // Si el bloque cruza el final del bucle, una nota a partir de ese punto ya es de
    // la vuelta siguiente: su posición real es ppq - longitud del bucle. Entre
    // loopSeamPpq y loopSeamMaxPpq no se sabe exactamente: se miran las dos. Justo
    // antes del final también (±1 semicorchea): esa nota está al lado de la primera
    // del bucle, que FL ya tiene.
    const bool loopKnown  = loopLengthBeats >= 1.0;
    const bool maybeNext  = loopKnown && ppq >= loopSeamPpq - kRepeatToleranceBeats;
    const bool surelyNext = loopKnown && ppq >= loopSeamMaxPpq;
    const double wrapped  = ppq - loopLengthBeats;

    bool repeat = ! surelyNext && matchesEarlierPass (note, ppq, loopPass);
    if (! repeat && maybeNext)
        repeat = matchesEarlierPass (note, wrapped, loopPass + 1);

    if (! repeat)
    {
        if (! surelyNext) rememberLoopTrigger (note, ppq, loopPass);
        if (maybeNext)    rememberLoopTrigger (note, wrapped, loopPass + 1);
    }
    return repeat;
}

bool SmartKeysAudioProcessor::matchesEarlierPass (int note, double ppq, int pass) const noexcept
{
    const auto n = (size_t) note;
    for (int i = 0; i < (int) loopTriggerCount[n]; ++i)
    {
        const auto& r = loopTriggers[n][(size_t) i];
        if (r.pass < pass && std::abs (r.ppq - ppq) <= kRepeatToleranceBeats)
            return true;
    }
    return false;
}

void SmartKeysAudioProcessor::rememberLoopTrigger (int note, double ppq, int pass) noexcept
{
    const auto n = (size_t) note;
    auto& slot = loopTriggers[n][(size_t) loopTriggerNext[n]];
    slot.ppq = ppq;
    slot.pass = pass;
    loopTriggerNext[n] = (uint8_t) ((loopTriggerNext[n] + 1) % kLoopTriggersPerNote);
    loopTriggerCount[n] = (uint8_t) juce::jmin (kLoopTriggersPerNote, loopTriggerCount[n] + 1);
}

void SmartKeysAudioProcessor::drainScreenNotes()
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
                routeNoteOn (MidiOutputRouter::Source::Screen, e.note, (juce::uint8) e.velocity, 0, true);
            else
                routeNoteOff (MidiOutputRouter::Source::Screen, e.note, 0);
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

    // Reloj del bloque (el teclado virtual programa sus notas con él)
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();
    const double msPerSample = 1000.0 / currentSampleRate;
    maxBlockMs = juce::jmax (maxBlockMs, (double) numSamples * msPerSample);
    if (! processedAnyBlock)
    {
        processedAnyBlock = true;
        pluginInUse.store (true);   // el temporizador crea "SmartKeys Chords"
    }
    if (bypassActive)
    {
        bypassActive = false;
        resetEchoState();   // tras el bypass todo empezó apagado
    }

    updateEngineIfNeeded();
    voiceTracker.setGateThresholdDb (pMicGate->load());
    const double strumMs = (double) pStrumMs->load();
    const bool strumDown = juce::roundToInt (pStrumDir->load()) == StrumDir::Descending;
    strum.setStrum (strumMs, strumDown);
    portStrum.setStrum (strumMs, strumDown);

    // Capacidad reservada: tras el primer bloque no hay asignaciones
    routedMidi.clear();
    routedMidi.ensureSize (kMidiReserveBytes);
    expandedChordMidi.clear();
    expandedChordMidi.ensureSize (kMidiReserveBytes);
    portRoutedMidi.clear();
    portRoutedMidi.ensureSize (kMidiReserveBytes);
    portExpandedMidi.clear();
    portExpandedMidi.ensureSize (kMidiReserveBytes);

    // Transporte del DAW: ¿está grabando? (posición en negras de este bloque)
    bool recordingNow = false, hostLoop = false;
    double blockPpq = 0.0, ppqPerSample = 0.0, hostLoopStart = 0.0, hostLoopEnd = 0.0;
    if (auto* transport = getPlayHead())
    {
        if (const auto position = transport->getPosition())
        {
            const auto ppq = position->getPpqPosition();
            const auto bpm = position->getBpm();
            if (position->getIsPlaying() && position->getIsRecording() && ppq.hasValue() && bpm.hasValue() && *bpm > 0.0)
            {
                recordingNow = true;
                blockPpq = *ppq;
                ppqPerSample = *bpm / (60.0 * currentSampleRate);

                // Si el DAW dice dónde está su bucle, el final se sabe desde la primera vuelta
                if (position->getIsLooping())
                    if (const auto loop = position->getLoopPoints())
                        if (loop->ppqEnd - loop->ppqStart >= 1.0
                             && blockPpq >= loop->ppqStart - 0.25 && blockPpq < loop->ppqEnd + 0.25)
                        {
                            hostLoop = true;
                            hostLoopStart = loop->ppqStart;
                            hostLoopEnd = loop->ppqEnd;
                        }
            }
        }
    }

    hostRecording.store (recordingNow, std::memory_order_relaxed);   // "· REC" en la ventana

    if (recordingNow && ! recordingTake)
    {
        loopTriggerCount.fill (0);   // toma nueva: la primera vuelta sale entera
        loopTriggerNext.fill (0);
        loopPass = 0;
        loopLengthBeats = 0.0;
    }
    else if (recordingNow && blockPpq < lastBlockEndPpq - 0.25)
    {
        ++loopPass;                  // el patrón volvió a empezar (o el cursor fue hacia atrás)

        // El final del bucle estaba dentro del bloque anterior; vuelta a vuelta se acota
        const double jump = lastBlockEndPpq - blockPpq;
        if (loopLengthBeats > 0.0 && std::abs (jump - loopLengthBeats) <= 0.01)
        {
            loopSeamPpq    = juce::jmax (loopSeamPpq, lastBlockStartPpq);
            loopSeamMaxPpq = juce::jmin (loopSeamMaxPpq, lastBlockEndPpq);
        }
        else
        {
            loopLengthBeats = jump;
            loopSeamPpq     = lastBlockStartPpq;
            loopSeamMaxPpq  = lastBlockEndPpq;
        }
    }
    else if (recordingNow && loopLengthBeats > 0.0 && blockPpq >= loopSeamMaxPpq)
    {
        loopLengthBeats = 0.0;       // siguió más allá: no era un bucle (se movió el cursor)
    }

    if (hostLoop)
    {
        loopLengthBeats = hostLoopEnd - hostLoopStart;
        loopSeamPpq = loopSeamMaxPpq = hostLoopEnd;
    }

    recordingTake = recordingNow;
    if (recordingNow)
    {
        lastBlockStartPpq = blockPpq;
        lastBlockEndPpq = blockPpq + (double) numSamples * ppqPerSample;
    }

    //==========================================================================
    // PASO 1 — ELEGIR DE DÓNDE LLEGA LA VOZ (antes de tocar el buffer)
    //   a) La entrada del plugin, si el DAW manda audio por ella (sidechain).
    //   b) Si llega en silencio digital (Channel Rack de FL Studio sin ruteo):
    //      el micrófono del sistema, que escribe su propio hilo de CoreAudio en
    //      un búfer circular lock-free.
    //==========================================================================
    const bool micOn = pMicOn->load() > 0.5f;

    if (! micOn && micWasOn)
        routeNoteOff (MidiOutputRouter::Source::Voice, 0, 0);
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
            routeNoteOff (MidiOutputRouter::Source::Voice, 0, 0);
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
    drainScreenNotes();

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

    // Teclado virtual activo: hay que reconocer su eco si el DAW lo devuelve
    const bool portActive = portFeedActive.load (std::memory_order_acquire);
    echoGroupInBlock = false;
    if (portActive)
    {
        updateEchoEvidence (blockStartMs);

        // ¿Llegan en este bloque varias notas que el teclado acaba de enviar? Es
        // un acorde que vuelve entero (una persona no pulsa así)
        int candidates = 0;
        uint64_t seen[2] = { 0, 0 };
        for (const auto meta : midiMessages)
        {
            const auto* d = meta.data;
            if (meta.numBytes < 3 || d == nullptr || (d[0] & 0xF0) != 0x90 || (d[2] & 0x7F) == 0)
                continue;
            const int n = d[1] & 0x7F;
            const uint64_t bit = (uint64_t) 1 << (n & 63);
            if ((seen[n >> 6] & bit) != 0)
                continue;
            seen[n >> 6] |= bit;
            const double arrivalMs = blockStartMs + (double) juce::jlimit (0, lastSample, meta.samplePosition) * msPerSample;
            candidates += isPortEchoCandidate (n, arrivalMs) ? 1 : 0;
        }
        echoGroupInBlock = candidates >= 2;
    }

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
                const double arrivalMs = blockStartMs + (double) pos * msPerSample;
                if (portActive && isPortEchoNoteOn (note, arrivalMs))
                {
                    // El DAW devolvió "SmartKeys Chords" a SmartKeys: no es una tecla
                    if (echoSuppressed[(size_t) note] < 255)
                        ++echoSuppressed[(size_t) note];
                    continue;
                }

                midiKeyDown[(size_t) note] = true;
                const bool toPort = ! recordingNow || ! isLoopRepeat (note, blockPpq + (double) pos * ppqPerSample);
                routeNoteOn (MidiOutputRouter::Source::Midi, note, (juce::uint8) velocity, pos, toPort);
            }
            else if ((type == 0x80 || type == 0x90) && size >= 3)
            {
                const double arrivalMs = blockStartMs + (double) pos * msPerSample;
                if (portActive && isPortEchoNoteOff (note, arrivalMs))
                {
                    --echoSuppressed[(size_t) note];
                    continue;
                }

                midiKeyDown[(size_t) note] = false;
                suspectTrigger[(size_t) note] = false;
                routeNoteOff (MidiOutputRouter::Source::Midi, note, pos);
            }
            else if (status >= 0x80 && status < 0xF0 && size <= 3)
            {
                // CC, sustain, pitch bend, aftertouch... al canal 1
                if (type == 0xB0 && (note == 120 || note == 123))
                {
                    routeReleaseAll (pos);   // All Sound Off / All Notes Off del DAW
                    midiKeyDown.fill (false);
                    suspectTrigger.fill (false);
                }

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
                routeNoteOn (MidiOutputRouter::Source::Voice, e.note, (juce::uint8) e.velocity, pos, true);
            else
                routeNoteOff (MidiOutputRouter::Source::Voice, e.note, pos);
            ++voiceIdx;
        }
    }

    //==========================================================================
    // PASO 5 — STRUM: entrada progresiva de las notas (ASC / DESC), por muestra
    //==========================================================================
    strum.process (routedMidi, expandedChordMidi, numSamples);
    portStrum.process (portRoutedMidi, portExpandedMidi, numSamples);

    // Teclado virtual "SmartKeys Chords": las mismas notas, en su instante (sólo un
    // FIFO lock-free; al exportar fuera de tiempo real no se envía nada).
    // Reloj de muestras: cada bloque empieza donde terminó el anterior, aunque el
    // DAW procese varios bloques seguidos de golpe (FL Studio parte su búfer); se
    // acerca despacio al reloj real. Si se queda atrás (el audio se detuvo un
    // momento) salta al reloj real: las notas nunca salen antes de su hora ni se
    // amontonan; si va muy por delante (el DAW adelantó trabajo), también.
    if (portActive && ! isNonRealtime())
    {
        const double drift = blockStartMs - portClockMs;   // > 0: el reloj de muestras va atrasado
        if (! portClockValid || drift > 0.5 * maxBlockMs || -drift > juce::jmax (100.0, 4.0 * maxBlockMs))
        {
            portClockMs = blockStartMs;
            portClockValid = true;
        }
        else
        {
            portClockMs += 0.01 * drift;
        }

        feedChordPort (portExpandedMidi, numSamples, portClockMs);
        portClockMs += (double) numSamples * msPerSample;
    }
    else
    {
        portClockValid = false;
    }

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
// Bypass: silencio y, al entrar, se apaga todo lo que sonaba (salida MIDI,
// piano y teclado virtual): ningún instrumento enganchado queda con notas colgadas.
void SmartKeysAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    midiMessages.clear();

    if (bypassActive)
        return;
    bypassActive = true;

    routedMidi.clear();
    expandedChordMidi.clear();
    routeReleaseAll (0);
    portRoutedMidi.clear();   // el teclado lo apaga requestReleaseAll()
    portStrum.reset();
    voiceTracker.forceRelease (0);

    const int numSamples = buffer.getNumSamples();
    if (numSamples > 0)
    {
        strum.process (routedMidi, expandedChordMidi, numSamples);
        midiMessages.swapWith (expandedChordMidi);
    }
    else
    {
        strum.reset();
    }

    pianoSynth.allNotesOff();
    portFeed.requestReleaseAll();
    midiKeyDown.fill (false);
    suspectTrigger.fill (false);

    for (auto& a : activeNotesBitmask)
        a.store (0, std::memory_order_release);
    voiceChordSounding.store (false);
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
    state.setProperty ("chordKeyboard", chordPortEnabled.load(), nullptr);

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

    // Teclado "SmartKeys Chords": lo que se guardó con el proyecto (1.6+)
    if (tree.hasProperty ("chordKeyboard"))
    {
        const bool enabled = (bool) tree.getProperty ("chordKeyboard");
        if (juce::MessageManager::existsAndIsCurrentThread())
            setChordPortEnabled (enabled);
        else
            chordPortEnabled.store (enabled);   // el temporizador lo aplica
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartKeysAudioProcessor();
}
