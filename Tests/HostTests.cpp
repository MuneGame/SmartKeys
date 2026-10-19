/*
  ==============================================================================

    HostTests.cpp
    SmartKeys — prueba de integración: carga el VST3 COMPILADO como lo haría
    un DAW y comprueba, bloque a bloque, lo que sale del plugin:

      1. Cada tecla sale por MIDI OUT como el ACORDE COMPLETO (lo que pinta el
         Piano Roll del instrumento destino), siempre por el canal MIDI 1.
      2. Siempre a la vez: el piano interno suena (con su GAIN) y los acordes
         completos salen por MIDI. Ya no existen OUTPUT MODE, A4, MICROTUNING
         ni MIDI CH, y los proyectos guardados con la 1.3 / 1.4 se abren sin
         restos de lo quitado.
      3. STRUM: las notas del acorde entran escalonadas ASC/DESC, exactas a la
         muestra, y un NoteOff anticipado no deja notas colgadas.
      4. COMPLEX CHORDS: 5–7 notas por tecla y el mismo número de NoteOff.
      5. Voz por la entrada del DAW: una voz cantando La3 dispara el acorde La
         menor, se suelta al callar, y la voz cruda NUNCA sale.
      6. MIC VOICE sin sidechain: con la entrada del plugin en silencio digital
         (FL Studio), el plugin lee él mismo el micrófono del sistema (en Linux,
         el simulado de SystemMicInput; en macOS, CoreAudio de verdad), aunque
         vaya a otra frecuencia de muestreo que el DAW. Si el DAW manda voz por
         la entrada, manda la entrada.
      7. Bloques de 0 a 512 muestras (FL Studio), cero asignaciones de memoria
         en el processBlock real y concurrencia audio + estado + micrófono.

    Uso:  SmartKeysHostTests <SmartKeys.vst3> [--stress <segundos>] [--system-mic-only]

  ==============================================================================
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <set>
#include <map>
#include <vector>
#include <thread>
#include <random>
#include <cerrno>
#include <cstdlib>

using namespace juce;

//==============================================================================
// Contador de asignaciones de memoria de TODO el proceso (host + plugin), sólo
// en el hilo que lo activa: se reemplaza malloc de glibc. Así se comprueba que
// el processBlock del VST3 compilado no pide memoria. (No con sanitizers.)
#if JUCE_LINUX && ! defined (__SANITIZE_ADDRESS__) && ! defined (__SANITIZE_THREAD__)
 #define SK_COUNT_ALLOCATIONS 1
 #include <execinfo.h>
static __thread int tlsCountAllocations = 0;
static __thread int tlsInHook = 0;
static std::atomic<long> gProcessAllocations { 0 };

// Con SK_ALLOC_TRACE=1 se guarda la pila de las primeras asignaciones contadas
static bool gTraceAllocations = false;
static void* gTraceStacks[8][24];
static int gTraceDepth[8];
static std::atomic<int> gNumTraces { 0 };

static void noteAllocation()
{
    gProcessAllocations.fetch_add (1);
    if (gTraceAllocations && tlsInHook == 0)
    {
        const int slot = gNumTraces.fetch_add (1);
        if (slot < 8)
        {
            ++tlsInHook;
            gTraceDepth[slot] = backtrace (gTraceStacks[slot], 24);
            --tlsInHook;
        }
    }
}

static void printAllocationTraces()
{
    for (int i = 0; i < jmin (8, gNumTraces.load()); ++i)
    {
        std::cout << "     --- asignación " << (i + 1) << ":" << std::endl;
        backtrace_symbols_fd (gTraceStacks[i], gTraceDepth[i], 1);
    }
}

extern "C"
{
    void* __libc_malloc (size_t);
    void* __libc_calloc (size_t, size_t);
    void* __libc_realloc (void*, size_t);
    void* __libc_memalign (size_t, size_t);

    void* malloc (size_t n)            { if (tlsCountAllocations && ! tlsInHook) noteAllocation(); return __libc_malloc (n); }
    void* calloc (size_t a, size_t b)  { if (tlsCountAllocations && ! tlsInHook) noteAllocation(); return __libc_calloc (a, b); }
    void* realloc (void* p, size_t n)  { if (tlsCountAllocations && ! tlsInHook) noteAllocation(); return __libc_realloc (p, n); }
    void* memalign (size_t a, size_t n){ if (tlsCountAllocations && ! tlsInHook) noteAllocation(); return __libc_memalign (a, n); }
    void* aligned_alloc (size_t a, size_t n) { return memalign (a, n); }
    int posix_memalign (void** out, size_t a, size_t n)
    {
        *out = memalign (a, n);
        return *out != nullptr ? 0 : ENOMEM;
    }
}
#else
 #define SK_COUNT_ALLOCATIONS 0
static int tlsCountAllocations = 0;
static std::atomic<long> gProcessAllocations { 0 };
#endif

namespace
{
int numChecks = 0, numFailures = 0;

// Los textos son UTF-8: se imprimen tal cual (sin pasar por juce::String)
void check (bool ok, const char* what)
{
    ++numChecks;
    if (! ok) ++numFailures;
    std::cout << (ok ? "  ok     " : "  FALLO  ") << what << std::endl;
}

String describe (const std::set<int>& notes)
{
    String s;
    for (int n : notes)
        s << (s.isEmpty() ? "" : " ") << MidiMessage::getMidiNoteName (n, true, true, 4);
    return "{" + s + "}";
}

std::set<int> pitchClasses (const std::set<int>& notes)
{
    std::set<int> pcs;
    for (int n : notes) pcs.insert (n % 12);
    return pcs;
}

//==============================================================================
struct BlockResult
{
    std::vector<MidiMessage> midi;
    float outputRms = 0.0f;

    std::set<int> notesOn (int channel = 0) const
    {
        std::set<int> s;
        for (auto& m : midi)
            if (m.isNoteOn() && (channel == 0 || m.getChannel() == channel)) s.insert (m.getNoteNumber());
        return s;
    }
    std::set<int> notesOff (int channel = 0) const
    {
        std::set<int> s;
        for (auto& m : midi)
            if (m.isNoteOff() && (channel == 0 || m.getChannel() == channel)) s.insert (m.getNoteNumber());
        return s;
    }
    std::set<int> channelsUsed() const
    {
        std::set<int> s;
        for (auto& m : midi)
            if (m.isNoteOnOrOff()) s.insert (m.getChannel());
        return s;
    }
    int countNoteOn() const  { int c = 0; for (auto& m : midi) c += m.isNoteOn() ? 1 : 0;  return c; }
    int countNoteOff() const { int c = 0; for (auto& m : midi) c += m.isNoteOff() ? 1 : 0; return c; }
};

//==============================================================================
// Transporte del "DAW" de la prueba (posición, tempo, PLAY / REC)
struct Transport : public AudioPlayHead
{
    bool playing = false, recording = false;
    double ppq = 0.0, bpm = 120.0, sampleRate = 48000.0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples ((int64) std::llround (ppq * 60.0 / bpm * sampleRate));
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (playing);
        info.setIsRecording (recording);
        info.setPpqPosition (ppq);
        info.setTimeInSeconds (ppq * 60.0 / bpm);
        return info;
    }
};

//==============================================================================
class Host
{
public:
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 512;

    bool load (const String& path)
    {
        VST3PluginFormatHeadless format;
        OwnedArray<PluginDescription> types;
        format.findAllTypesForFile (types, path);
        if (types.isEmpty())
        {
            std::cout << "No se encontró ningún plugin en " << path << std::endl;
            return false;
        }

        String error;
        plugin = format.createInstanceFromDescription (*types[0], sampleRate, blockSize, error);
        if (plugin == nullptr)
        {
            std::cout << "No se pudo cargar: " << error << std::endl;
            return false;
        }

        std::cout << "Plugin: " << types[0]->name << "  (" << types[0]->category << ", "
                  << (types[0]->isInstrument ? "instrumento" : "efecto") << ")" << std::endl;

        plugin->enableAllBuses();
        plugin->setPlayHead (&transport);
        plugin->prepareToPlay (sampleRate, blockSize);
        numIns  = plugin->getTotalNumInputChannels();
        numOuts = plugin->getTotalNumOutputChannels();
        buffer.setSize (jmax (numIns, numOuts, 1), blockSize);
        std::cout << "Buses: " << numIns << " canal(es) de entrada (micrófono), " << numOuts << " de salida" << std::endl;
        return true;
    }

    void unload()
    {
        if (plugin != nullptr)
            plugin->releaseResources();
        plugin.reset();
    }

    AudioPluginInstance& instance() { return *plugin; }

    void getState (MemoryBlock& mb)               { plugin->getStateInformation (mb); }
    void setState (const MemoryBlock& mb)         { plugin->setStateInformation (mb.getData(), (int) mb.getSize()); }
    bool isInstrument() const { return plugin != nullptr && plugin->getPluginDescription().isInstrument; }
    int getNumInputs() const  { return numIns; }

    bool hasParameter (const String& name)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (128) == name)
                return true;
        return false;
    }

    // Estado interno del plugin (el host VST3 lo envuelve en
    // <VST3PluginState><IComponent>base64</IComponent></VST3PluginState>)
    std::unique_ptr<XmlElement> getPluginStateXml()
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        auto xml = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (xml != nullptr && xml->hasTagName ("VST3PluginState"))
        {
            std::unique_ptr<XmlElement> inner;
            if (auto* component = xml->getChildByName ("IComponent"))
            {
                MemoryBlock data;
                if (data.fromBase64Encoding (component->getAllSubText().trim()))
                    inner = AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
            }
            return inner;
        }
        return xml;
    }

    // Carga en el plugin un estado interno (como si FL abriera un proyecto)
    void setPluginStateXml (const XmlElement& pluginState)
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        auto wrapper = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (wrapper == nullptr || ! wrapper->hasTagName ("VST3PluginState"))
            return;
        if (auto* component = wrapper->getChildByName ("IComponent"))
        {
            MemoryBlock inner;
            AudioProcessor::copyXmlToBinary (pluginState, inner);
            component->deleteAllTextElements();
            component->addTextElement (inner.toBase64Encoding());
            MemoryBlock out;
            AudioProcessor::copyXmlToBinary (*wrapper, out);
            plugin->setStateInformation (out.getData(), (int) out.getSize());
        }
    }

    AudioProcessorParameter* findParameter (const String& name)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (128) == name)
                return p;
        std::cout << "  (no existe el parámetro \"" << name << "\")" << std::endl;
        return nullptr;
    }

    void setChoice (const String& name, int index)
    {
        if (auto* p = findParameter (name))
        {
            const int steps = p->getNumSteps();
            p->setValueNotifyingHost (steps > 1 ? (float) index / (float) (steps - 1) : (float) index);
        }
    }

    void setText (const String& name, const String& text)
    {
        if (auto* p = findParameter (name))
            p->setValueNotifyingHost (p->getValueForText (text));
    }

    void setBool (const String& name, bool on)
    {
        if (auto* p = findParameter (name))
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    // Procesa un bloque: eventos MIDI de entrada + (opcional) una voz senoidal en la entrada
    BlockResult run (const std::vector<MidiMessage>& events = {}, double sineHz = 0.0, float amplitude = 0.0f)
    {
        buffer.clear();
        if (sineHz > 0.0)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = amplitude * (float) std::sin (phase);
                phase += MathConstants<double>::twoPi * sineHz / sampleRate;
                for (int ch = 0; ch < numIns; ++ch)
                    buffer.setSample (ch, i, s);
            }
        }

        midi.clear();
        int pos = 16;
        for (auto& e : events)
            midi.addEvent (e, pos++);

        return process();
    }

    // Bloque con eventos en muestras concretas (pos relativa al bloque)
    BlockResult runAt (const std::vector<std::pair<int, MidiMessage>>& events)
    {
        buffer.clear();
        midi.clear();
        for (auto& e : events)
            midi.addEvent (e.second, e.first);
        return process();
    }

    BlockResult process (int numSamples = blockSize)
    {
        AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);

        ++tlsCountAllocations;
        plugin->processBlock (view, midi);
        --tlsCountAllocations;

        BlockResult r;
        for (const auto meta : midi)
        {
            if (meta.samplePosition < 0 || meta.samplePosition >= jmax (1, numSamples) || (numSamples == 0))
                ++eventsOutOfRange;
            auto m = meta.getMessage();
            m.setTimeStamp ((double) (sampleClock + meta.samplePosition));
            r.midi.push_back (m);
        }
        for (int ch = 0; ch < numOuts && numSamples > 0; ++ch)
            r.outputRms = jmax (r.outputRms, view.getRMSLevel (ch, 0, numSamples));

        sampleClock += numSamples;
        if (transport.playing || transport.recording)
            transport.ppq += (double) numSamples * transport.bpm / (60.0 * sampleRate);
        return r;
    }

    // Bloque de 'numSamples' muestras (FL Studio usa tamaños variables, incluso 0)
    BlockResult runSized (int numSamples, const std::vector<std::pair<int, MidiMessage>>& events, double sineHz = 0.0, float amplitude = 0.0f)
    {
        buffer.clear();
        if (sineHz > 0.0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = amplitude * (float) std::sin (phase);
                phase += MathConstants<double>::twoPi * sineHz / sampleRate;
                for (int ch = 0; ch < numIns; ++ch)
                    buffer.setSample (ch, i, v);
            }
        }
        midi.clear();
        for (auto& e : events)
            midi.addEvent (e.second, jlimit (0, jmax (0, numSamples - 1), e.first));
        return process (numSamples);
    }

    int eventsOutOfRange = 0;

    Transport transport;

    // Varios bloques: acumula el MIDI y devuelve el RMS máximo
    BlockResult runBlocks (int numBlocks, double sineHz = 0.0, float amplitude = 0.0f)
    {
        BlockResult total;
        for (int b = 0; b < numBlocks; ++b)
        {
            auto r = run ({}, sineHz, amplitude);
            total.midi.insert (total.midi.end(), r.midi.begin(), r.midi.end());
            total.outputRms = jmax (total.outputRms, r.outputRms);
        }
        return total;
    }

    // Deja que el piano termine de sonar
    void settle (int numBlocks = 250) { runBlocks (numBlocks); }

    // Bloques a TIEMPO REAL (el micrófono del sistema entrega audio a su ritmo)
    // dejando correr el hilo de mensajes: el Timer del plugin abre el micrófono.
    BlockResult runRealtime (double seconds, double sineHz = 0.0, float amplitude = 0.0f)
    {
        BlockResult total;
        const double blockMs = 1000.0 * blockSize / sampleRate;
        double next = Time::getMillisecondCounterHiRes();
        const double end = next + seconds * 1000.0;
        while (Time::getMillisecondCounterHiRes() < end)
        {
            auto r = run ({}, sineHz, amplitude);
            total.midi.insert (total.midi.end(), r.midi.begin(), r.midi.end());
            total.outputRms = jmax (total.outputRms, r.outputRms);
            next += blockMs;
            const int wait = (int) (next - Time::getMillisecondCounterHiRes());
            if (wait > 0)
                MessageManager::getInstance()->runDispatchLoopUntil (wait);
        }
        return total;
    }

private:
    std::unique_ptr<AudioPluginInstance> plugin;
    AudioBuffer<float> buffer;
    MidiBuffer midi;
    int numIns = 0, numOuts = 0;
    double phase = 0.0;
    int64 sampleClock = 0;
};

//==============================================================================
void testInstrument (const String& path)
{
    std::cout << "\n=== SmartKeys (instrumento) ===" << std::endl;
    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    check (host.isInstrument(), "se registra como instrumento (Channel Rack / pista MIDI)");
    check (host.getNumInputs() >= 1, "tiene entrada de micrófono (sidechain)");

    // Do mayor, SMART CHORDS, tríadas
    host.setChoice ("Key", 0);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Chord Type", 0);
    host.setBool ("Mic Voice Controller", false);
    host.settle (4);

    std::cout << "\n-- 1. Piano Roll: una tecla -> acorde completo por MIDI OUT" << std::endl;
    {
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });   // D4
        std::cout << "     D4 -> " << describe (r.notesOn()) << std::endl;
        check (r.notesOn (1) == std::set<int> { 62, 65, 69 }, "D4 en Do mayor sale como Dm completo {D4 F4 A4} por el canal 1");
        auto tail = host.runBlocks (3);
        check (tail.outputRms > 1.0e-3f || r.outputRms > 1.0e-3f, "el piano interno suena a la vez");
        auto off = host.run ({ MidiMessage::noteOff (1, 62) });
        check (off.notesOff (1) == std::set<int> { 62, 65, 69 }, "al soltar la tecla salen los 3 NoteOff");
    }

    std::cout << "\n-- 2. Canal MIDI fijo: todo sale por el canal 1" << std::endl;
    {
        check (! host.hasParameter ("MIDI Out Channel"), "ya no existe el selector MIDI CH");
        auto r = host.run ({ MidiMessage::noteOn (5, 67, (uint8) 90), MidiMessage::controllerEvent (5, 64, 127) });   // G4 por el canal 5
        std::cout << "     G4 (entra por el canal 5) -> " << describe (r.notesOn()) << std::endl;
        bool allOnOne = ! r.midi.empty();
        for (auto& m : r.midi)
            allOnOne = allOnOne && m.getChannel() == 1;
        check (r.notesOn (1) == std::set<int> { 67, 71, 74 } && allOnOne,
               "aunque la tecla llegue por otro canal, el acorde G y el pedal salen por el canal 1");
        auto off = host.run ({ MidiMessage::noteOff (5, 67), MidiMessage::controllerEvent (5, 64, 0) });
        check (off.notesOff (1) == std::set<int> { 67, 71, 74 }, "y sus NoteOff también");
        host.settle();
    }

    std::cout << "\n-- 3. Piano interno + MIDI siempre a la vez (sin OUTPUT MODE, A4 ni MICROTUNING)" << std::endl;
    {
        check (! host.hasParameter ("Output Mode") && ! host.hasParameter ("Reference Pitch")
                 && ! host.hasParameter ("Scale Microtuning"),
               "OUTPUT MODE, A4 (Reference Pitch) y MICROTUNING ya no existen");
        check (host.hasParameter ("Master Gain") && host.hasParameter ("Strum Speed") && host.hasParameter ("Mic Voice Controller")
                 && host.hasParameter ("Strum Direction") && host.hasParameter ("Mic Gate"),
               "GAIN, MIC VOICE, GATE, STRUM SPEED y ASC/DESC siguen ahí");

        host.run();
        auto r2 = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });   // A4 -> Am
        auto tail2 = host.runBlocks (4);
        check (r2.notesOn (1) == std::set<int> { 69, 72, 76 }, "el acorde Am completo sale por MIDI");
        const float loud = jmax (r2.outputRms, tail2.outputRms);
        check (loud > 1.0e-3f, "y suena el piano interno a la vez");
        host.run ({ MidiMessage::noteOff (1, 69) });
        host.settle();

        // GAIN baja el piano, pero el MIDI sale igual
        if (auto* g = host.findParameter ("Master Gain"))
            g->setValueNotifyingHost (g->getValueForText ("-30"));
        host.settle (4);
        auto r3 = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });
        auto tail3 = host.runBlocks (4);
        const float quiet = jmax (r3.outputRms, tail3.outputRms);
        std::cout << "     RMS con GAIN -3 dB: " << loud << "   con -30 dB: " << quiet << std::endl;
        check (r3.notesOn (1) == std::set<int> { 69, 72, 76 } && quiet > 0.0f && quiet < loud * 0.1f,
               "GAIN -30 dB: el piano baja ~27 dB y el acorde sigue saliendo completo por MIDI");
        host.run ({ MidiMessage::noteOff (1, 69) });
        if (auto* g = host.findParameter ("Master Gain"))
            g->setValueNotifyingHost (g->getValueForText ("-3"));
        host.settle();
    }

    std::cout << "\n-- 3b. Un proyecto guardado con la 1.3 / 1.4 se abre sin restos de lo quitado" << std::endl;
    {
        auto old = host.getPluginStateXml();
        check (old != nullptr && old->hasTagName ("SmartKeysState"), "el estado del plugin se puede leer");
        if (old != nullptr)
        {
            // Lo que guardaba la 1.3: OUTPUT MODE = MIDI OUT, A4 = 442 Hz, MICROTUNING,
            // puerto MIDI, ajustes de la captura y una progresión
            old->setAttribute ("stateVersion", 3);
            old->removeAttribute ("customMask");   // la máscara se recalcula para la tónica guardada
            old->setAttribute ("virtualPort", 1);
            old->setAttribute ("captureSync", 1);
            old->setAttribute ("quantize", 3);
            for (auto [id, value] : { std::pair<const char*, double> { "outputMode", 1.0 }, { "refPitch", 442.0 },
                                      { "microTuning", 1.0 }, { "midiOutCh", 4.0 } })
            {
                auto* param = old->createNewChildElement ("PARAM");
                param->setAttribute ("id", id);
                param->setAttribute ("value", value);
            }
            for (auto* param : old->getChildWithTagNameIterator ("PARAM"))
                if (param->getStringAttribute ("id") == "rootKey")
                    param->setAttribute ("value", 2.0);   // Re
            auto* progression = old->createNewChildElement ("Progression");
            progression->createNewChildElement ("Chord")->setAttribute ("notes", "60 64 67");

            host.setPluginStateXml (*old);
            host.settle (2);

            auto loaded = host.getPluginStateXml();
            bool clean = loaded != nullptr && loaded->getChildByName ("Progression") == nullptr;
            for (auto* attr : { "virtualPort", "captureSync", "quantize" })
                clean = clean && loaded != nullptr && ! loaded->hasAttribute (attr);
            if (loaded != nullptr)
                for (auto* param : loaded->getChildWithTagNameIterator ("PARAM"))
                    for (auto* gone : { "outputMode", "refPitch", "microTuning", "midiOutCh" })
                        clean = clean && param->getStringAttribute ("id") != gone;
            check (clean, "se descartan OUTPUT MODE, A4, MICROTUNING, MIDI CH, el puerto y la progresión");

            auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });   // D4 en Re mayor -> D mayor
            auto tail = host.runBlocks (4);
            check (r.notesOn (1) == std::set<int> { 62, 66, 69 } && r.channelsUsed() == std::set<int> { 1 },
                   "la tónica guardada (Re) se respeta: D4 -> D mayor, por el canal 1 aunque el proyecto tuviera CH 5");
            check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "aunque el proyecto tenía MIDI OUT, el piano suena");
            host.run ({ MidiMessage::noteOff (1, 62) });
            host.setChoice ("Key", 0);
            host.settle();
        }
    }

    std::cout << "\n-- 4. COMPLEX CHORDS" << std::endl;
    {
        host.setChoice ("Mode", 2);
        host.setChoice ("Complex Variation", 1);   // Neo-Soul
        host.run();
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 110) });   // D4 -> Dm11
        const auto notes = r.notesOn (1);
        std::cout << "     D4 -> " << describe (notes) << std::endl;
        bool inScale = true;
        for (int n : notes)
            inScale = inScale && (std::set<int> { 0, 2, 4, 5, 7, 9, 11 }.count (n % 12) > 0);
        check (notes.size() >= 5 && notes.size() <= 7, "COMPLEX: la tecla sale como acorde de 5 a 7 notas por MIDI");
        check (inScale, "COMPLEX: todas las notas pertenecen a Do mayor");
        auto off = host.run ({ MidiMessage::noteOff (1, 62) });
        check (off.notesOff (1) == notes, "COMPLEX: salen todos los NoteOff del acorde");
        host.setChoice ("Mode", 0);
        host.settle();
    }

    std::cout << "\n-- 4b. STRUM: entrada progresiva ASC / DESC (exacta a la muestra)" << std::endl;
    {
        host.setChoice ("Mode", 0);
        if (auto* p = host.findParameter ("Strum Speed"))
        {
            p->setValueNotifyingHost (p->getValueForText ("100"));   // el plugin convierte "100" ms a su rango
            std::cout << "     Strum = " << p->getCurrentValueAsText() << std::endl;
        }
        host.setChoice ("Strum Direction", 0);
        host.settle (4);

        auto onTimes = [] (const BlockResult& r)
        {
            std::map<int, double> t;
            for (auto& m : r.midi)
                if (m.isNoteOn() && t.count (m.getNoteNumber()) == 0)
                    t[m.getNoteNumber()] = m.getTimeStamp();
            return t;
        };

        auto r = host.runAt ({ { 100, MidiMessage::noteOn (1, 65, (uint8) 100) } });   // F4 -> F A C
        auto tail = host.runBlocks (30);
        r.midi.insert (r.midi.end(), tail.midi.begin(), tail.midi.end());
        auto t = onTimes (r);
        const double t0 = t.count (65) ? t[65] : -1.0;
        std::cout << "     ASC 100 ms: F4 +0  A4 +" << (t[69] - t0) << "  C5 +" << (t[72] - t0) << " muestras" << std::endl;
        check (t.size() == 3 && std::abs (t[69] - t0 - 5520.0) < 1.0 && std::abs (t[72] - t0 - 12480.0) < 1.0,
               "ASC: F4 -> A4 (+115 ms) -> C5 (+260 ms), cada nota entra más lenta que la anterior");
        auto off = host.run ({ MidiMessage::noteOff (1, 65) });
        check (off.notesOff (1) == std::set<int> { 65, 69, 72 }, "al soltar, las 3 notas terminan juntas");

        host.setChoice ("Strum Direction", 1);
        host.settle (4);
        auto rd = host.runAt ({ { 100, MidiMessage::noteOn (1, 65, (uint8) 100) } });
        auto taild = host.runBlocks (30);
        rd.midi.insert (rd.midi.end(), taild.midi.begin(), taild.midi.end());
        auto td = onTimes (rd);
        check (td.size() == 3 && td[72] < td[69] && td[69] < td[65], "DESC: C5 -> A4 -> F4");
        host.run ({ MidiMessage::noteOff (1, 65) });

        // Soltar antes de que termine el barrido: nada queda colgado
        auto re = host.runAt ({ { 100, MidiMessage::noteOn (1, 65, (uint8) 100) }, { 2000, MidiMessage::noteOff (1, 65) } });
        auto taile = host.runBlocks (30);
        re.midi.insert (re.midi.end(), taile.midi.begin(), taile.midi.end());
        check (re.countNoteOn() == 1 && re.countNoteOff() == 1, "NoteOff anticipado: las notas pendientes se cancelan (0 notas colgadas)");

        if (auto* p = host.findParameter ("Strum Speed"))
            p->setValueNotifyingHost (0.0f);
        host.setChoice ("Strum Direction", 0);
        host.settle();
    }

    std::cout << "\n-- 5. La voz cruda nunca sale por el plugin" << std::endl;
    {
        host.setBool ("Mic Voice Controller", false);
        auto r = host.runBlocks (20, 220.0, 0.5f);
        check (r.outputRms < 1.0e-4f, "con voz fuerte en la entrada y sin notas, la salida está en silencio (buffer.clear())");
    }

    std::cout << "\n-- 6. Voz -> acorde por la entrada del DAW (sidechain)" << std::endl;
    {
        host.setChoice ("Mic Gate", 0);     // umbral mínimo: no influye en la prueba
        host.setBool ("Mic Voice Controller", true);
        host.run();

        auto sing = host.runBlocks (40, 220.0, 0.3f);   // La3 cantado ~0.4 s
        const auto on = sing.notesOn (1);
        std::cout << "     voz La3 (220 Hz) -> " << describe (on) << std::endl;
        check (on.size() == 3 && pitchClasses (on) == std::set<int> { 9, 0, 4 }, "cantar La3 dispara el acorde La menor completo por MIDI");
        check (sing.outputRms > 1.0e-3f, "y el piano interno lo toca");

        auto quiet = host.runBlocks (30);
        check (! on.empty() && quiet.notesOff (1) == on, "al dejar de cantar se sueltan todas las notas del acorde");
        host.setBool ("Mic Voice Controller", false);
        host.settle();
    }

    std::cout << "\n-- 7. Bloques de tamaño variable y de 0 muestras (como FL Studio)" << std::endl;
    {
        host.setChoice ("Mode", 0);
        host.setText ("Strum Speed", "60");
        host.setBool ("Mic Voice Controller", true);
        host.eventsOutOfRange = 0;

        std::mt19937 rng (7);
        std::set<int> held;
        int zeroBlocksWithMidi = 0, totalEvents = 0, inputNoteOns = 0, outputNoteOns = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const int n = rng() % 9 == 0 ? 0 : 1 + (int) (rng() % 512);
            std::vector<std::pair<int, MidiMessage>> ev;
            if (n > 0 && rng() % 4 == 0)
            {
                const int note = 48 + (int) (rng() % 36);
                const int pos = (int) (rng() % (unsigned) n);
                if (held.count (note) > 0) { ev.push_back ({ pos, MidiMessage::noteOff (1, note) }); held.erase (note); }
                else                       { ev.push_back ({ pos, MidiMessage::noteOn (1, note, (uint8) 100) }); held.insert (note); ++inputNoteOns; }
            }
            if (n > 0 && rng() % 97 == 0)
                ev.push_back ({ 0, MidiMessage::controllerEvent (1, 64, 127) });
            auto r = host.runSized (n, ev, rng() % 3 == 0 ? 220.0 : 0.0, 0.3f);
            totalEvents += (int) r.midi.size();
            outputNoteOns += r.countNoteOn();
            if (n == 0 && ! r.midi.empty())
                ++zeroBlocksWithMidi;
        }
        std::vector<std::pair<int, MidiMessage>> offs;
        for (int note : held)
            offs.push_back ({ 0, MidiMessage::noteOff (1, note) });
        host.runSized (256, offs);
        host.setBool ("Mic Voice Controller", false);
        host.setText ("Strum Speed", "0");
        host.settle();

        std::cout << "     4000 bloques de 0 a 512 muestras: " << inputNoteOns << " teclas -> " << outputNoteOns
                  << " NoteOn de salida (" << totalEvents << " eventos MIDI)" << std::endl;
        check (host.eventsOutOfRange == 0, "cada evento MIDI de salida cae dentro de su bloque (0 <= posición < tamaño)");
        check (zeroBlocksWithMidi == 0, "un bloque de 0 muestras no saca ningún evento MIDI");
    }

   #if SK_COUNT_ALLOCATIONS
    std::cout << "\n-- 8. processBlock del VST3 compilado: cero asignaciones de memoria" << std::endl;
    {
        host.setText ("Strum Speed", "80");
        host.setBool ("Mic Voice Controller", true);

        std::mt19937 rng (3);
        std::set<int> held;
        auto scenarioBlock = [&] (int i)
        {
            const int n = 32 + (int) (rng() % 481);
            std::vector<std::pair<int, MidiMessage>> ev;
            if (rng() % 3 == 0)
            {
                const int note = 48 + (int) (rng() % 30);
                if (held.count (note) > 0) { ev.push_back ({ 0, MidiMessage::noteOff (1, note) }); held.erase (note); }
                else                       { ev.push_back ({ 0, MidiMessage::noteOn (1, note, (uint8) 90) }); held.insert (note); }
            }
            if (i % 400 == 0)
                ev.push_back ({ 0, MidiMessage::controllerEvent (1, 123, 0) });   // All Notes Off del DAW
            host.runSized (n, ev, (i / 100) % 2 == 0 ? 220.0 : 0.0, 0.3f);
        };

        for (int i = 0; i < 300; ++i)
            scenarioBlock (i);   // calentamiento (los buffers alcanzan su capacidad)

        // Calentamiento del HOST de prueba: sus listas de eventos VST3 crecen la
        // primera vez que un bloque trae muchos eventos. El caso más grande: las
        // 30 teclas en COMPLEX CHORDS y un All Notes Off que las suelta todas.
        {
            host.setChoice ("Mode", 2);
            std::vector<std::pair<int, MidiMessage>> burst;
            for (int note = 48; note < 78; ++note)
                burst.push_back ({ 0, MidiMessage::noteOn (1, note, (uint8) 90) });
            host.runSized (512, burst);
            host.runBlocks (4);
            host.runSized (512, { { 0, MidiMessage::controllerEvent (1, 123, 0) } });
            host.runBlocks (2);
            host.setChoice ("Mode", 0);
            held.clear();

            // Un bloque con 600 eventos de control (pasan tal cual a la salida)
            std::vector<std::pair<int, MidiMessage>> many;
            for (int k = 0; k < 600; ++k)
                many.push_back ({ k % 512, MidiMessage::controllerEvent (1, 1, k % 128) });
            host.runSized (512, many);
            host.runBlocks (2);
        }

        {
            void* warm[4];
            backtrace (warm, 4);   // la primera llamada carga libgcc (asigna): fuera del contador
        }
        gTraceAllocations = SystemStats::getEnvironmentVariable ("SK_ALLOC_TRACE", {}).isNotEmpty();
        gProcessAllocations = 0;
        for (int i = 300; i < 3300; ++i)
        {
            if (i % 250 == 0)
            {
                host.setChoice ("Mode", (i / 250) % 3);                  // SMART / SINGLE / COMPLEX
                host.setChoice ("Scale", (i / 250) % 12);
                host.setText ("Strum Speed", String ((i / 250) % 5 * 40));
                host.setChoice ("Strum Direction", (i / 250) % 2);
            }
            scenarioBlock (i);
        }
        const long allocs = gProcessAllocations.load();
        std::cout << "     3000 bloques (notas, STRUM, voz, All Notes Off, cambios de modo/escala): "
                  << allocs << " asignaciones" << std::endl;
        check (allocs == 0, "el hilo de audio nunca pide memoria (ni realloc, ni new, ni juce::String)");
        if (gTraceAllocations)
            printAllocationTraces();
        gTraceAllocations = false;

        std::vector<std::pair<int, MidiMessage>> offs;
        for (int note : held)
            offs.push_back ({ 0, MidiMessage::noteOff (1, note) });
        host.runSized (256, offs);
        host.setBool ("Mic Voice Controller", false);
        host.setText ("Strum Speed", "0");
        host.setChoice ("Mode", 0);
        host.setChoice ("Scale", 0);
        host.setChoice ("Strum Direction", 0);
        host.settle();
    }
   #endif

    host.unload();
}

//==============================================================================
// Concurrencia: un hilo de "audio" llama a processBlock sin parar (bloques de
// tamaño variable, notas, voz) mientras el hilo
// de mensajes guarda / carga el estado y cambia parámetros, como hace FL Studio.
// Pensada para ejecutarse también con AddressSanitizer / ThreadSanitizer.
void testConcurrency (const String& path, double seconds)
{
    std::cout << "\n=== Concurrencia: audio + mensajes durante " << seconds << " s ===" << std::endl;
    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    host.setText ("Strum Speed", "70");
    host.setBool ("Mic Voice Controller", true);
    host.eventsOutOfRange = 0;

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 };
    std::thread audio ([&]
    {
        std::mt19937 rng (11);
        std::set<int> held;
        while (! stop.load())
        {
            const int n = rng() % 13 == 0 ? 0 : 16 + (int) (rng() % 497);
            std::vector<std::pair<int, MidiMessage>> ev;
            if (n > 0 && rng() % 3 == 0)
            {
                const int note = 40 + (int) (rng() % 50);
                if (held.count (note) > 0) { ev.push_back ({ (int) (rng() % (unsigned) n), MidiMessage::noteOff (1, note) }); held.erase (note); }
                else                       { ev.push_back ({ (int) (rng() % (unsigned) n), MidiMessage::noteOn (1, note, (uint8) 100) }); held.insert (note); }
            }
            host.runSized (n, ev, rng() % 2 == 0 ? 220.0 : 0.0, 0.25f);
            ++blocks;
        }
        std::vector<std::pair<int, MidiMessage>> offs;
        for (int note : held)
            offs.push_back ({ 0, MidiMessage::noteOff (1, note) });
        host.runSized (256, offs);
    });

    struct Hammer : public Timer
    {
        Hammer (Host& h, double end) : host (h), endMs (end) {}
        void timerCallback() override
        {
            ++iterations;
            MemoryBlock state;
            host.getState (state);
            if (rng() % 3 == 0 && previous.getSize() > 0)
                host.setState (previous);
            previous = state;
            host.setChoice ("Mode", (int) (rng() % 3));
            host.setChoice ("Scale", (int) (rng() % 12));
            host.setChoice ("Key", (int) (rng() % 12));
            host.setText ("Strum Speed", String ((int) (rng() % 200)));
            host.setChoice ("Strum Direction", (int) (rng() % 2));
            host.setText ("Master Gain", String (-(int) (rng() % 40)));
            if (rng() % 9 == 0)
                host.setBool ("Mic Voice Controller", rng() % 2 == 0);   // abre / cierra el micrófono del sistema
            if (Time::getMillisecondCounterHiRes() > endMs)
            {
                stopTimer();
                MessageManager::getInstance()->stopDispatchLoop();
            }
        }
        Host& host;
        double endMs;
        int iterations = 0;
        MemoryBlock previous;
        std::mt19937 rng { 5 };
    };

    Hammer hammer (host, Time::getMillisecondCounterHiRes() + seconds * 1000.0);
    hammer.startTimer (7);
    MessageManager::getInstance()->runDispatchLoop();
    stop = true;
    audio.join();

    std::cout << "     " << blocks.load() << " bloques de audio, " << hammer.iterations
              << " rondas de estado/parámetros en el hilo de mensajes" << std::endl;
    check (blocks.load() > 200 && hammer.iterations > 20, "los dos hilos trabajaron a la vez");
    check (host.eventsOutOfRange == 0, "con todo a la vez, ningún evento MIDI fuera de su bloque");
    host.unload();
}

//==============================================================================
// MIC VOICE sin sidechain: la entrada del plugin llega en silencio digital
// (Channel Rack de FL Studio) y el plugin lee él mismo el micrófono del sistema.
void testSystemMic (const String& path)
{
    std::cout << "\n=== MIC VOICE sin sidechain: micrófono del sistema ===" << std::endl;

   #if JUCE_LINUX
    // Micrófono simulado: La3 (220 Hz) a 44.1 kHz, mientras el "DAW" va a 48 kHz
    setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3", 1);
   #endif

    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    host.setChoice ("Key", 0);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Chord Type", 0);
    host.setChoice ("Mic Gate", 0);
    host.setBool ("Mic Voice Controller", true);

    auto sing = host.runRealtime (2.0);   // entrada del plugin: silencio digital
    const auto on = sing.notesOn (1);
    std::cout << "     entrada del plugin en silencio, micrófono del sistema con La3 -> " << describe (on) << std::endl;
    check (on.size() == 3 && pitchClasses (on) == std::set<int> { 9, 0, 4 },
           "MIC VOICE lee el micrófono del sistema sin ruteo: La3 -> La menor completo por MIDI");
    check (sing.outputRms > 1.0e-3f, "y el piano interno lo toca");
    check (sing.channelsUsed() == std::set<int> { 1 }, "por el canal 1");

    // Si el DAW manda voz por la entrada del plugin, manda esa: Do4 -> Do mayor
    auto daw = host.runRealtime (1.2, 261.63, 0.3);
    const auto onDaw = daw.notesOn (1);
    std::cout << "     el DAW manda Do4 por la entrada -> " << describe (onDaw) << std::endl;
    check (pitchClasses (onDaw) == std::set<int> { 0, 4, 7 } && daw.notesOff (1) == on,
           "con audio en la entrada del plugin, se usa esa voz (se suelta La menor, suena Do mayor)");

    // La entrada vuelve al silencio: el micrófono del sistema vuelve solo
    auto back = host.runRealtime (5.0);
    const auto onBack = back.notesOn (1);
    std::cout << "     la entrada vuelve al silencio -> " << describe (onBack) << std::endl;
    check (pitchClasses (onBack) == std::set<int> { 9, 0, 4 }, "al callar la entrada, vuelve a sonar la voz del micrófono del sistema");

    // MIC VOICE apagado: se sueltan las notas y nada más se dispara
    host.setBool ("Mic Voice Controller", false);
    auto offR = host.runRealtime (1.0);
    check (offR.notesOff (1) == onBack && offR.countNoteOn() == 0, "MIC VOICE OFF: se sueltan las notas y la voz ya no dispara");

   #if JUCE_LINUX
    // Voz intermitente (1 s cantando, 1 s en silencio): dispara y suelta
    setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3:2.0:0.5", 1);
    host.setBool ("Mic Voice Controller", true);
    auto phrase = host.runRealtime (4.5);
    std::cout << "     voz intermitente: " << phrase.countNoteOn() << " NoteOn, " << phrase.countNoteOff() << " NoteOff" << std::endl;
    check (phrase.countNoteOn() >= 6 && phrase.countNoteOff() >= 3, "cada frase dispara el acorde y cada silencio lo suelta");
    host.setBool ("Mic Voice Controller", false);
    host.runRealtime (0.5);
    setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3", 1);
   #endif

   #if SK_COUNT_ALLOCATIONS
    // El processBlock real con el micrófono del sistema activo: 0 asignaciones
    // (frases de 0.5 s: dispara y suelta acordes mientras se cuenta)
    setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3:1.0:0.5", 1);
    host.setBool ("Mic Voice Controller", true);
    host.runRealtime (1.5);
    gProcessAllocations = 0;
    auto counted = host.runRealtime (2.5);
    const long allocs = gProcessAllocations.load();
    std::cout << "     2.5 s leyendo el micrófono del sistema: " << counted.countNoteOn() << " NoteOn, "
              << allocs << " asignaciones en el hilo de audio" << std::endl;
    check (allocs == 0 && counted.countNoteOn() >= 3, "leer el micrófono del sistema no asigna memoria en processBlock");
    host.setBool ("Mic Voice Controller", false);
    host.runRealtime (0.5);
   #endif

    host.unload();

   #if JUCE_LINUX
    unsetenv ("SMARTKEYS_FAKE_MIC");
   #endif
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "Uso: SmartKeysHostTests <SmartKeys.vst3> [--stress <segundos>] [--system-mic-only]" << std::endl;
        return 2;
    }

    // Rutas absolutas (juce::File no admite rutas relativas)
    const auto cwd = File::getCurrentWorkingDirectory();
    double stressSeconds = 4.0;
    bool systemMicOnly = false;
    StringArray paths;
    for (int i = 1; i < argc; ++i)
    {
        const auto arg = String::fromUTF8 (argv[i]);
        if (arg == "--stress" && i + 1 < argc)
            stressSeconds = String::fromUTF8 (argv[++i]).getDoubleValue();
        else if (arg == "--system-mic-only")
            systemMicOnly = true;
        else
            paths.add (cwd.getChildFile (arg).getFullPathName());
    }

    if (! systemMicOnly)
        testInstrument (paths[0]);

    testSystemMic (paths[0]);

    if (! systemMicOnly && stressSeconds > 0.0)
    {
       #if JUCE_LINUX
        setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3", 1);   // el micrófono también se abre y cierra
       #endif
        testConcurrency (paths[0], stressSeconds);
       #if JUCE_LINUX
        unsetenv ("SMARTKEYS_FAKE_MIC");
       #endif
    }

    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    return numFailures == 0 ? 0 : 1;
}
