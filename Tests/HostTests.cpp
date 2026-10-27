/*
  ==============================================================================

    HostTests.cpp
    SmartKeys — prueba de integración: carga el VST3 COMPILADO como lo haría
    un DAW y comprueba, bloque a bloque, lo que sale del plugin:

      1. Cada tecla sale por MIDI OUT como el ACORDE COMPLETO (lo que pinta el
         Piano Roll del instrumento destino), siempre por el canal MIDI 1.
      2. Siempre a la vez: el instrumento cargado en la ranura suena (con GAIN)
         y los acordes completos salen por MIDI. Ya no existen OUTPUT MODE, A4,
         MICROTUNING ni MIDI CH, y los proyectos guardados con la 1.3 / 1.4 se
         abren sin restos de lo quitado.
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
      8. Teclado MIDI virtual "SmartKeys Chords" (lo que FL Studio graba en el
         Piano Roll del instrumento enganchado): cada tecla sale como el acorde
         completo, a la vez o con el desfase exacto del STRUM; repetir rápido
         una tecla nunca se pierde; si el DAW devuelve el teclado a SmartKeys
         (bucle), los ecos no disparan acordes ni dejan notas colgadas; bypass,
         cerrar el plugin y exportar. En macOS se abre la fuente CoreMIDI de
         verdad; en Linux el teclado escribe en un archivo.
      9. 2.0, la ranura de instrumento (SmartKeys como contenedor): sin
         instrumento no suena nada (ya no hay piano propio) y el MIDI sale
         igual; con el instrumento de prueba cargado (VST3, y AU en macOS) éste
         recibe el acorde COMPLETO en la muestra exacta de la salida MIDI, con
         el STRUM exacto, en trozos si el bloque es más grande que el
         preparado; su audio sale por SmartKeys con GAIN; la latencia llega al
         DAW; BYPASS suelta sus notas y lo calla; su sonido vuelve con el
         proyecto; quitarlo, un instrumento que falta (no se pierde al guardar),
         NaN del instrumento, carga protegida tras un cierre, cero asignaciones
         y cargas / descargas con el audio corriendo.

    Uso:  SmartKeysHostTests <SmartKeys.vst3> --synth <SmartKeysTestSynth.vst3> [--synth-au]
                             [--stress <segundos>] [--system-mic-only] [--port-only] [--host-only]

  ==============================================================================
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <map>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <random>
#include <cerrno>
#include <cstdlib>

#if JUCE_MAC
 #include "MacActivity.h"
#endif

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
    double loopBeats = 0.0;   // > 0: el patrón vuelve al principio cada tantas negras (FL en modo patrón)
    bool reportLoop = false;  // el DAW dice dónde está su bucle (puntos de bucle)
                              // Ojo: el bloque NO se corta en el final del bucle (se vuelve al principio después)

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
        if (reportLoop && loopBeats > 0.0)
        {
            info.setIsLooping (true);
            info.setLoopPoints (LoopPoints { 0.0, loopBeats });
        }
        return info;
    }
};

//==============================================================================
class Host
{
public:
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 512;
    static constexpr int kMaxBlock = 4096;

    std::vector<float>* capture = nullptr;   // si no es nulo: guarda el canal izquierdo de la salida

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
        buffer.setSize (jmax (numIns, numOuts, 1), kMaxBlock);   // bloques de hasta 4096 (más grandes que el preparado)
        midi.ensureSize (65536);   // como un DAW: el MIDI del bloque ya reservado (lo que el plugin manda
                                   // se copia aquí; si creciera, la asignación sería del "DAW", no del plugin)
        std::cout << "Buses: " << numIns << " canal(es) de entrada (micrófono), " << numOuts << " de salida" << std::endl;
        return true;
    }

    void unload()
    {
        if (plugin != nullptr)
            plugin->releaseResources();
        plugin.reset();
    }

    // El DAW cambia el tamaño de búfer (el plugin se prepara otra vez)
    void prepare (int maxBlock)
    {
        plugin->releaseResources();
        plugin->prepareToPlay (sampleRate, jmin (maxBlock, kMaxBlock));
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
        {
            const float* d = view.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i]))
                    ++nonFiniteSamples;
            r.outputRms = jmax (r.outputRms, view.getRMSLevel (ch, 0, numSamples));
        }
        if (capture != nullptr && numOuts > 0)
            capture->insert (capture->end(), view.getReadPointer (0), view.getReadPointer (0) + numSamples);

        sampleClock += numSamples;
        if (transport.playing || transport.recording)
        {
            transport.ppq += (double) numSamples * transport.bpm / (60.0 * sampleRate);
            if (transport.loopBeats > 0.0 && transport.ppq >= transport.loopBeats)
                transport.ppq -= transport.loopBeats;
        }
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
    long nonFiniteSamples = 0;   // NaN / infinito en la salida (nunca debe haber)
    int64 getSampleClock() const noexcept { return sampleClock; }

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

    // Deja que el instrumento termine de sonar
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
// 2.0: el instrumento de prueba (Tests/TestSynth.cpp) en la ranura de SmartKeys
//==============================================================================
String gSynthPath;             // SmartKeysTestSynth.vst3 (--synth)
bool gSynthAU = false;         // macOS: también como Audio Unit registrado (--synth-au)
File gSynthLog;                // lo que apunta el instrumento de prueba
constexpr int kSynthLatency = 96;

int fourCC (const char* s) { return (int) (((uint32) (uint8) s[0] << 24) | ((uint32) (uint8) s[1] << 16) | ((uint32) (uint8) s[2] << 8) | (uint32) (uint8) s[3]); }

PluginDescription testSynthDescription (bool asAudioUnit = false)
{
    if (asAudioUnit)
    {
        PluginDescription d;
        d.name = d.descriptiveName = "SmartKeysTestSynth";
        d.manufacturerName = "MIWN Test";
        d.pluginFormatName = "AudioUnit";
        d.category = "Synth";
        d.isInstrument = true;
        d.fileOrIdentifier = "AudioUnit:Synths/aumu,Tsyn,Miwt";
        d.uniqueId = d.deprecatedUid = fourCC ("aumu") ^ fourCC ("Tsyn") ^ fourCC ("Miwt");
        d.numOutputChannels = 2;
        return d;
    }

    VST3PluginFormatHeadless format;
    OwnedArray<PluginDescription> types;
    format.findAllTypesForFile (types, gSynthPath);
    return types.isEmpty() ? PluginDescription() : *types[0];
}

std::unique_ptr<XmlElement> slotXml (Host& host)
{
    auto xml = host.getPluginStateXml();
    if (xml == nullptr)
        return nullptr;
    if (auto* h = xml->getChildByName ("HostedPlugin"))
        return std::make_unique<XmlElement> (*h);
    return nullptr;
}

String slotStatus (Host& host)
{
    auto h = slotXml (host);
    return h != nullptr ? h->getStringAttribute ("status") : String ("empty");
}

// Deja correr el hilo de mensajes (el temporizador de SmartKeys y el del instrumento)
// mientras el "DAW" sigue procesando bloques
void pumpWhileRunning (Host& host, int ms)
{
    const auto end = Time::getMillisecondCounterHiRes() + ms;
    while (Time::getMillisecondCounterHiRes() < end)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        host.run();
    }
}

bool waitForSlot (Host& host, const String& wanted, int timeoutMs = 10000)
{
    const auto end = Time::getMillisecondCounterHiRes() + timeoutMs;
    while (Time::getMillisecondCounterHiRes() < end)
    {
        pumpWhileRunning (host, 40);
        if (slotStatus (host) == wanted)
            return true;
    }
    auto h = slotXml (host);
    std::cout << "     (la ranura quedó en \"" << slotStatus (host) << "\": "
              << (h != nullptr ? h->toString (XmlElement::TextFormat().singleLine().withoutHeader()) : String ("-")) << ")" << std::endl;
    return false;
}

// Pone en la ranura un instrumento (como al abrir un proyecto, un preset o elegirlo
// en LOAD PLUGIN) y espera a que se cargue
void setSlot (Host& host, const PluginDescription* d, bool bypass = false, const String& stateBase64 = {})
{
    auto xml = host.getPluginStateXml();
    if (xml == nullptr)
        return;
    xml->deleteAllChildElementsWithTagName ("HostedPlugin");
    if (d != nullptr)
    {
        auto* h = xml->createNewChildElement ("HostedPlugin");
        h->setAttribute ("bypass", bypass ? 1 : 0);
        h->addChildElement (d->createXml().release());
        if (stateBase64.isNotEmpty())
            h->createNewChildElement ("STATE")->addTextElement (stateBase64);
    }
    host.setPluginStateXml (*xml);
}

bool loadInSlot (Host& host, const PluginDescription& d, int timeoutMs = 10000)
{
    setSlot (host, &d);
    return waitForSlot (host, "loaded", timeoutMs);
}

void setSlotBypass (Host& host, bool on)
{
    auto xml = host.getPluginStateXml();
    if (xml == nullptr)
        return;
    if (auto* h = xml->getChildByName ("HostedPlugin"))
        h->setAttribute ("bypass", on ? 1 : 0);
    host.setPluginStateXml (*xml);
}

// Lo que apuntó el instrumento de prueba (las líneas desde 'fromLine')
struct SynthEvent
{
    int64 at = 0;
    int status = 0, d1 = 0, d2 = 0;
    bool isNoteOn() const  { return (status & 0xF0) == 0x90 && d2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && d2 == 0); }
};

struct SynthLog
{
    int instance = -1;                // la última instancia creada
    std::vector<SynthEvent> events;   // de esa instancia
    StringArray lines;                // todas las líneas nuevas (sin el "#id ")

    bool has (const String& prefix) const
    {
        for (auto& l : lines)
            if (l.startsWith (prefix))
                return true;
        return false;
    }

    String find (const String& prefix) const
    {
        for (auto& l : lines)
            if (l.startsWith (prefix))
                return l;
        return {};
    }

    std::map<int, int64> noteOnTimes() const
    {
        std::map<int, int64> t;
        for (auto& e : events)
            if (e.isNoteOn() && t.count (e.d1) == 0)
                t[e.d1] = e.at;
        return t;
    }

    std::set<int> notesOn() const  { std::set<int> s; for (auto& e : events) if (e.isNoteOn()) s.insert (e.d1); return s; }
    std::set<int> notesOff() const { std::set<int> s; for (auto& e : events) if (e.isNoteOff()) s.insert (e.d1); return s; }
    int countNoteOn() const        { int n = 0; for (auto& e : events) n += e.isNoteOn() ? 1 : 0; return n; }
};

int synthLogLines()
{
    MessageManager::getInstance()->runDispatchLoopUntil (60);   // lo pendiente del instrumento, ya escrito
    StringArray l;
    gSynthLog.readLines (l);
    l.removeEmptyStrings();
    return l.size();
}

SynthLog readSynthLog (int fromLine)
{
    MessageManager::getInstance()->runDispatchLoopUntil (80);   // el instrumento escribe cada 10 ms

    StringArray all;
    gSynthLog.readLines (all);
    all.removeEmptyStrings();

    SynthLog log;
    for (auto& l : all)
        if (l.contains (" CREATED"))
            log.instance = l.fromFirstOccurrenceOf ("#", false, false).upToFirstOccurrenceOf (" ", false, false).getIntValue();

    for (int i = jmax (0, fromLine); i < all.size(); ++i)
    {
        const auto& l = all[i];
        const int id = l.fromFirstOccurrenceOf ("#", false, false).upToFirstOccurrenceOf (" ", false, false).getIntValue();
        const auto body = l.fromFirstOccurrenceOf (" ", false, false);
        log.lines.add (body);
        if (id == log.instance && body.startsWith ("EV "))
        {
            StringArray t;
            t.addTokens (body, " ", {});
            if (t.size() >= 5)
                log.events.push_back ({ t[1].getLargeIntValue(), t[2].getIntValue(), t[3].getIntValue(), t[4].getIntValue() });
        }
    }
    return log;
}

float goertzel (const std::vector<float>& x, size_t from, size_t n, double sr, double hz)
{
    const double w = MathConstants<double>::twoPi * hz / sr, coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < jmin (x.size(), from + n); ++i)
    {
        const double v = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = v;
    }
    return (float) (std::sqrt (jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / (double) n);
}

// Instante (en muestras del DAW) de cada NoteOn de la salida MIDI
std::map<int, int64> midiNoteOnTimes (const BlockResult& r)
{
    std::map<int, int64> t;
    for (auto& m : r.midi)
        if (m.isNoteOn() && t.count (m.getNoteNumber()) == 0)
            t[m.getNoteNumber()] = (int64) m.getTimeStamp();
    return t;
}

// ¿El instrumento recibió cada nota a la vez que la salida MIDI (con el mismo desfase constante)?
bool sameTiming (const std::map<int, int64>& midi, const std::map<int, int64>& synth)
{
    if (midi.empty() || midi.size() != synth.size())
        return false;
    const int64 offset = synth.begin()->second - midi.at (synth.begin()->first);
    for (auto& [note, at] : midi)
        if (synth.count (note) == 0 || synth.at (note) - at != offset)
            return false;
    return true;
}

//==============================================================================
void testInstrument (const String& path)
{
    std::cout << "\n=== SmartKeys (instrumento) ===" << std::endl;
    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    check (host.isInstrument(), "se registra como instrumento (Channel Rack / pista MIDI)");
    check (host.getNumInputs() >= 1, "tiene entrada de micrófono (sidechain)");

    // 2.0: SmartKeys suena con el instrumento cargado en su ranura
    const auto synth = testSynthDescription();
    check (synth.fileOrIdentifier.isNotEmpty() && loadInSlot (host, synth), "el instrumento de prueba se carga en la ranura");

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
        check (tail.outputRms > 1.0e-3f || r.outputRms > 1.0e-3f, "el instrumento cargado suena a la vez");
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

    std::cout << "\n-- 3. Instrumento cargado + MIDI siempre a la vez (sin OUTPUT MODE, A4 ni MICROTUNING)" << std::endl;
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
        check (loud > 1.0e-3f, "y suena el instrumento cargado a la vez");
        host.run ({ MidiMessage::noteOff (1, 69) });
        host.settle();

        // GAIN baja el instrumento, pero el MIDI sale igual
        if (auto* g = host.findParameter ("Master Gain"))
            g->setValueNotifyingHost (g->getValueForText ("-30"));
        host.settle (4);
        auto r3 = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });
        auto tail3 = host.runBlocks (4);
        const float quiet = jmax (r3.outputRms, tail3.outputRms);
        std::cout << "     RMS con GAIN -3 dB: " << loud << "   con -30 dB: " << quiet << std::endl;
        check (r3.notesOn (1) == std::set<int> { 69, 72, 76 } && quiet > 0.0f && quiet < loud * 0.1f,
               "GAIN -30 dB: el instrumento baja ~27 dB y el acorde sigue saliendo completo por MIDI");
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
            check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "aunque el proyecto tenía MIDI OUT, el instrumento cargado suena");
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
        check (sing.outputRms > 1.0e-3f, "y el instrumento cargado lo toca");

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

std::map<int, int> countNoteOns (const std::vector<MidiMessage>& midi);
String describeCounts (const std::map<int, int>& c);

//==============================================================================
// 2.0: LA RANURA DE INSTRUMENTO. SmartKeys es un contenedor: toca el instrumento
// cargado con los acordes completos. Aquí se carga el instrumento de prueba,
// que apunta exactamente lo que recibe y cuándo.
void testHostedInstrument (const String& path)
{
    std::cout << "\n=== 2.0: instrumento cargado en la ranura de SmartKeys ===" << std::endl;
    const auto synth = testSynthDescription();
    if (synth.fileOrIdentifier.isEmpty())
    {
        check (false, "el instrumento de prueba (--synth) se puede describir");
        return;
    }

    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }
    host.setChoice ("Key", 0);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Chord Type", 0);
    host.setBool ("Mic Voice Controller", false);
    host.settle (4);

    std::cout << "\n-- H1. Sin instrumento: ya no hay piano propio; el MIDI sale igual" << std::endl;
    {
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
        auto tail = host.runBlocks (6);
        check (r.notesOn (1) == std::set<int> { 62, 65, 69 }, "D4 sale como Dm completo por MIDI OUT");
        check (jmax (r.outputRms, tail.outputRms) <= 0.0f, "con la ranura vacía SmartKeys no suena (no tiene piano propio)");
        check (slotStatus (host) == "empty" && host.instance().getLatencySamples() == 0, "ranura vacía: latencia 0");
        host.run ({ MidiMessage::noteOff (1, 62) });
        host.settle (2);
    }

    std::cout << "\n-- H2. LOAD PLUGIN: el instrumento se carga (en el hilo de mensajes) y se prepara" << std::endl;
    int mark = synthLogLines();
    const double t0 = Time::getMillisecondCounterHiRes();
    const bool loaded = loadInSlot (host, synth);
    std::cout << "     cargado en " << (int) (Time::getMillisecondCounterHiRes() - t0) << " ms" << std::endl;
    check (loaded, "el instrumento de prueba se carga en la ranura");
    if (! loaded)
    {
        host.unload();
        return;
    }
    pumpWhileRunning (host, 300);   // SmartKeys avisa al DAW de la latencia desde su temporizador
    {
        auto log = readSynthLog (mark);
        std::cout << "     " << log.find ("PREPARE") << "   latencia de SmartKeys: " << host.instance().getLatencySamples() << std::endl;
        check (log.has ("PREPARE 48000 512 realtime=1"), "se prepara con la frecuencia y el bloque del DAW (48 kHz, 512)");
        check (host.instance().getLatencySamples() == kSynthLatency,
               "SmartKeys declara al DAW la latencia del instrumento (96 muestras): el DAW la compensa");
    }

    std::cout << "\n-- H3. Una tecla -> el instrumento recibe el acorde COMPLETO, en la misma muestra" << std::endl;
    {
        std::vector<float> audio;
        host.capture = &audio;
        mark = synthLogLines();
        auto r = host.runAt ({ { 100, MidiMessage::noteOn (1, 62, (uint8) 100) } });
        auto tail = host.runBlocks (16);
        host.capture = nullptr;
        auto log = readSynthLog (mark);
        const auto synthTimes = log.noteOnTimes();
        std::cout << "     MIDI OUT " << describe (r.notesOn (1)) << "   instrumento " << describe (log.notesOn()) << std::endl;
        check (r.notesOn (1) == std::set<int> { 62, 65, 69 } && log.notesOn() == std::set<int> { 62, 65, 69 },
               "D4 -> el instrumento recibe D4 F4 A4 (el acorde completo, no sólo la tecla)");
        check (sameTiming (midiNoteOnTimes (r), synthTimes), "cada nota le llega en la muestra exacta de la salida MIDI");

        // Su audio sale por SmartKeys: el espectro tiene D, F y A
        const size_t from = 2048, n = 8192;
        const float d = goertzel (audio, from, n, Host::sampleRate, 293.66), f = goertzel (audio, from, n, Host::sampleRate, 349.23),
                    a = goertzel (audio, from, n, Host::sampleRate, 440.00), off = goertzel (audio, from, n, Host::sampleRate, 370.0);
        std::cout << "     RMS " << jmax (r.outputRms, tail.outputRms) << "   D " << d << "  F " << f << "  A " << a << "  (fuera: " << off << ")" << std::endl;
        check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "el instrumento suena por la salida de SmartKeys");
        check (d > off * 10.0f && f > off * 10.0f && a > off * 10.0f, "lo que suena es Re menor (D, F, A)");

        mark = synthLogLines();
        auto off2 = host.run ({ MidiMessage::noteOff (1, 62) });
        auto logOff = readSynthLog (mark);
        check (off2.notesOff (1) == std::set<int> { 62, 65, 69 } && logOff.notesOff() == std::set<int> { 62, 65, 69 },
               "al soltar, el instrumento recibe los 3 NoteOff");
        host.settle (8);
    }

    std::cout << "\n-- H3b. Una tecla, un acorde: nunca dos acordes superpuestos ni re-ataques" << std::endl;
    {
        auto noteOffCount = [] (const SynthLog& log) { int n = 0; for (auto& e : log.events) n += e.isNoteOff() ? 1 : 0; return n; };

        // a) La misma tecla otra vez sin haberla soltado (un controlador que rebota, dos
        //    entradas del DAW con el mismo teclado): el acorde sigue, no se corta ni se repite
        mark = synthLogLines();
        auto r = host.runAt ({ { 50, MidiMessage::noteOn (1, 62, (uint8) 100) }, { 300, MidiMessage::noteOn (1, 62, (uint8) 100) } });
        auto r2 = host.runAt ({ { 10, MidiMessage::noteOn (1, 62, (uint8) 90) } });
        r.midi.insert (r.midi.end(), r2.midi.begin(), r2.midi.end());
        auto held = host.runBlocks (4);
        r.midi.insert (r.midi.end(), held.midi.begin(), held.midi.end());
        auto log = readSynthLog (mark);
        std::cout << "     D4 tres veces sin soltar: salida MIDI " << describeCounts (countNoteOns (r.midi)) << ", "
                  << r.countNoteOff() << " NoteOff   instrumento " << log.countNoteOn() << " NoteOn, " << noteOffCount (log) << " NoteOff" << std::endl;
        check (countNoteOns (r.midi) == std::map<int, int> { { 62, 1 }, { 65, 1 }, { 69, 1 } } && r.countNoteOff() == 0,
               "NoteOn repetido sin su NoteOff: un solo Dm por MIDI OUT, sin cortes ni re-ataques");
        check (log.countNoteOn() == 3 && noteOffCount (log) == 0,
               "el instrumento recibe un solo acorde (3 NoteOn), sin tartamudeo");
        check (jmax (r.outputRms, held.outputRms) > 1.0e-3f, "y suena sin interrupción");

        mark = synthLogLines();
        auto off = host.run ({ MidiMessage::noteOff (1, 62) });
        auto logOff = readSynthLog (mark);
        check (off.countNoteOff() == 3 && off.notesOff (1) == std::set<int> { 62, 65, 69 } && noteOffCount (logOff) == 3,
               "un solo NoteOff suelta el acorde entero (nada colgado)");
        host.settle (4);

        // b) La misma tecla por dos canales a la vez (dos controladores, o el teclado de FL
        //    y el controlador): un acorde; se suelta una vez
        mark = synthLogLines();
        auto both = host.run ({ MidiMessage::noteOn (1, 60, (uint8) 100), MidiMessage::noteOn (2, 60, (uint8) 100) });
        auto firstOff = host.run ({ MidiMessage::noteOff (2, 60) });
        auto secondOff = host.run ({ MidiMessage::noteOff (1, 60) });
        auto logBoth = readSynthLog (mark);
        check (countNoteOns (both.midi) == std::map<int, int> { { 60, 1 }, { 64, 1 }, { 67, 1 } },
               "la misma tecla por dos canales: un solo acorde (C E G una vez)");
        check (firstOff.countNoteOff() == 3 && secondOff.midi.empty() && logBoth.countNoteOn() == 3 && noteOffCount (logBoth) == 3,
               "se suelta una sola vez; el segundo NoteOff no hace nada");

        // c) NoteOff suelto y NoteOn de velocidad 0 (= NoteOff)
        auto stray = host.run ({ MidiMessage::noteOff (1, 64) });
        check (stray.midi.empty(), "un NoteOff sin su NoteOn no saca nada");
        auto press = host.run ({ MidiMessage::noteOn (1, 67, (uint8) 100) });
        auto vel0 = host.run ({ MidiMessage (0x90, 67, 0) });
        check (press.countNoteOn() == 3 && vel0.notesOff (1) == std::set<int> { 67, 71, 74 }, "un NoteOn de velocidad 0 suelta como un NoteOff");

        // d) Soltar y volver a pulsar (aunque sea en el mismo bloque) sí son dos acordes
        mark = synthLogLines();
        auto again = host.runAt ({ { 20, MidiMessage::noteOn (1, 65, (uint8) 100) }, { 200, MidiMessage::noteOff (1, 65) },
                                   { 400, MidiMessage::noteOn (1, 65, (uint8) 100) } });
        auto logAgain = readSynthLog (mark);
        check (again.countNoteOn() == 6 && again.countNoteOff() == 3 && logAgain.countNoteOn() == 6,
               "soltar y volver a pulsar: dos acordes, el segundo después del NoteOff");
        host.run ({ MidiMessage::noteOff (1, 65) });
        host.settle (4);

        // e) El instrumento sólo recibe los acordes: el MIDI de entrada se reemplaza (una tecla fuera
        //    de la escala no le llega tal cual)
        mark = synthLogLines();
        auto out = host.run ({ MidiMessage::noteOn (1, 61, (uint8) 100) });   // C#4 en Do mayor
        auto logOut = readSynthLog (mark);
        std::cout << "     C#4 (fuera de Do mayor) -> salida MIDI " << describe (out.notesOn (1)) << "   instrumento " << describe (logOut.notesOn()) << std::endl;
        check (logOut.notesOn().count (61) == 0 && out.notesOn (1).count (61) == 0 && logOut.notesOn() == out.notesOn (1)
                 && logOut.countNoteOn() == 3,
               "la tecla cruda nunca llega al instrumento: sólo el acorde de la escala");
        host.run ({ MidiMessage::noteOff (1, 61) });
        host.settle (4);
    }

    std::cout << "\n-- H4. STRUM: el instrumento recibe el barrido exacto (ASC y DESC)" << std::endl;
    {
        host.setText ("Strum Speed", "100");
        for (int dir : { 0, 1 })
        {
            host.setChoice ("Strum Direction", dir);
            host.settle (2);
            mark = synthLogLines();
            auto r = host.runAt ({ { 100, MidiMessage::noteOn (1, 65, (uint8) 100) } });   // F4 -> F A C
            auto tail = host.runBlocks (30);
            r.midi.insert (r.midi.end(), tail.midi.begin(), tail.midi.end());
            auto log = readSynthLog (mark);
            const auto midiT = midiNoteOnTimes (r);
            const auto synthT = log.noteOnTimes();
            if (synthT.size() == 3)
                std::cout << "     " << (dir == 0 ? "ASC " : "DESC") << ": F4 +" << (synthT.at (65) - synthT.begin()->second)
                          << "  A4 +" << (synthT.at (69) - synthT.begin()->second) << "  C5 +" << (synthT.at (72) - synthT.begin()->second)
                          << " muestras (en el instrumento)" << std::endl;
            const bool order = synthT.size() == 3 && (dir == 0 ? (synthT.at (65) < synthT.at (69) && synthT.at (69) < synthT.at (72))
                                                                 : (synthT.at (72) < synthT.at (69) && synthT.at (69) < synthT.at (65)));
            check (order && sameTiming (midiT, synthT),
                   dir == 0 ? "ASC: F4 -> A4 -> C5 llegan al instrumento en la muestra exacta del STRUM"
                            : "DESC: C5 -> A4 -> F4 llegan al instrumento en la muestra exacta del STRUM");
            if (dir == 0 && synthT.size() == 3)
                check (synthT.at (69) - synthT.at (65) == 5520 && synthT.at (72) - synthT.at (65) == 12480,
                       "ASC 100 ms: +115 ms y +260 ms (5520 y 12480 muestras), como la salida MIDI");
            host.run ({ MidiMessage::noteOff (1, 65) });
            host.settle (4);
        }

        // Soltar a mitad del barrido: las notas pendientes no le llegan y nada queda colgado
        host.setChoice ("Strum Direction", 0);
        mark = synthLogLines();
        host.runAt ({ { 100, MidiMessage::noteOn (1, 65, (uint8) 100) }, { 2000, MidiMessage::noteOff (1, 65) } });
        host.runBlocks (30);
        auto log = readSynthLog (mark);
        check (log.countNoteOn() == 1 && log.notesOff() == std::set<int> { 65 },
               "NoteOff a mitad del STRUM: el instrumento sólo recibe la nota que ya entró, y su NoteOff");
        host.setText ("Strum Speed", "0");
        host.settle (4);
    }

    std::cout << "\n-- H5. COMPLEX CHORDS: el instrumento recibe las 5-7 notas" << std::endl;
    {
        host.setChoice ("Mode", 2);
        host.setChoice ("Complex Variation", 1);
        host.settle (2);
        mark = synthLogLines();
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 110) });
        auto log = readSynthLog (mark);
        std::cout << "     MIDI OUT " << describe (r.notesOn (1)) << "   instrumento " << describe (log.notesOn()) << std::endl;
        check (r.notesOn (1).size() >= 5 && log.notesOn() == r.notesOn (1), "el instrumento toca el acorde complejo completo");
        host.run ({ MidiMessage::noteOff (1, 62) });
        host.setChoice ("Mode", 0);
        host.settle (4);
    }

    std::cout << "\n-- H6. El DAW cambia el tamaño de búfer: el instrumento se prepara otra vez" << std::endl;
    {
        mark = synthLogLines();
        host.prepare (2048);
        // C4 -> C E G y, 1600 muestras después, A4 -> A C E (sin notas en común)
        auto r = host.runSized (2048, { { 100, MidiMessage::noteOn (1, 60, (uint8) 100) },
                                        { 1700, MidiMessage::noteOn (1, 69, (uint8) 100) } });
        auto log = readSynthLog (mark);
        const auto midiT = midiNoteOnTimes (r), synthT = log.noteOnTimes();
        std::cout << "     " << log.find ("PREPARE") << "   " << log.find ("MAXBLOCK") << std::endl;
        check (log.has ("PREPARE 48000 2048"), "SmartKeys prepara otra vez el instrumento con el búfer nuevo (2048)");
        check (! log.has ("OVERBLOCK"), "el instrumento nunca recibe un bloque más grande que el que se le preparó");
        check (synthT.size() == 6 && sameTiming (midiT, synthT), "en bloques de 2048, cada nota sigue en su muestra exacta (+1600 la segunda tecla)");
        host.runSized (2048, { { 0, MidiMessage::noteOff (1, 60) }, { 0, MidiMessage::noteOff (1, 69) } });
        mark = synthLogLines();
        host.prepare (Host::blockSize);
        host.settle (4);
        check (readSynthLog (mark).has ("PREPARE 48000 512"), "y de vuelta a 512");
    }

    std::cout << "\n-- H7. GAIN: el volumen del instrumento" << std::endl;
    {
        auto level = [&] (const char* db)
        {
            host.setText ("Master Gain", db);
            host.settle (4);
            auto r = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });
            auto tail = host.runBlocks (6);
            host.run ({ MidiMessage::noteOff (1, 69) });
            host.settle (6);
            return jmax (r.outputRms, tail.outputRms);
        };
        const float loud = level ("-3"), quiet = level ("-30");
        std::cout << "     RMS con GAIN -3 dB: " << loud << "   con -30 dB: " << quiet << std::endl;
        check (loud > 1.0e-3f && quiet > 0.0f && quiet < loud * 0.1f, "GAIN -30 dB baja el instrumento ~27 dB");
        host.setText ("Master Gain", "-3");
        host.settle (2);
    }

    std::cout << "\n-- H8. BYPASS: suelta sus notas y lo calla; el MIDI sigue saliendo" << std::endl;
    {
        host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });   // La menor sostenido
        host.runBlocks (4);
        mark = synthLogLines();
        setSlotBypass (host, true);
        auto first = host.runBlocks (2);
        auto silent = host.runBlocks (6);
        auto whileBypassed = host.run ({ MidiMessage::noteOn (1, 53, (uint8) 100) });   // F3 -> F A C
        auto log = readSynthLog (mark);
        check (log.notesOff().count (69) == 1 && log.notesOff().count (72) == 1 && log.notesOff().count (76) == 1,
               "al entrar en BYPASS, el instrumento recibe el NoteOff de lo que sonaba (nada se queda colgado)");
        check (silent.outputRms <= 0.0f && whileBypassed.outputRms <= 0.0f, "en BYPASS el instrumento no suena");
        check (whileBypassed.notesOn (1) == std::set<int> { 53, 57, 60 } && log.countNoteOn() == 0,
               "en BYPASS los acordes siguen saliendo por MIDI y el instrumento no recibe nada");
        check (slotXml (host) != nullptr && slotXml (host)->getIntAttribute ("bypass") == 1, "BYPASS se guarda con el proyecto");
        host.run ({ MidiMessage::noteOff (1, 69), MidiMessage::noteOff (1, 53) });

        setSlotBypass (host, false);
        host.settle (2);
        auto back = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
        auto backTail = host.runBlocks (4);
        check (jmax (back.outputRms, backTail.outputRms) > 1.0e-3f, "al quitar BYPASS vuelve a sonar");
        host.run ({ MidiMessage::noteOff (1, 62) });
        host.settle (6);
    }

    std::cout << "\n-- H9. El sonido del instrumento se guarda con el proyecto y vuelve al abrirlo" << std::endl;
    {
        host.run ({ MidiMessage::noteOn (1, 64, (uint8) 100) });
        host.run ({ MidiMessage::noteOff (1, 64) });
        mark = synthLogLines();
        auto saved = host.getPluginStateXml();
        auto savedSlot = slotXml (host);
        const auto stateLine = readSynthLog (mark).find ("STATE_SAVE");
        const int notes = stateLine.fromFirstOccurrenceOf ("notes=", false, false).getIntValue();
        std::cout << "     guardado: " << (savedSlot != nullptr ? savedSlot->toString (XmlElement::TextFormat().singleLine()).substring (0, 160) : String ("-"))
                  << "...  (" << stateLine << ")" << std::endl;
        check (savedSlot != nullptr && savedSlot->getStringAttribute ("status") == "loaded"
                 && savedSlot->getChildByName ("PLUGIN") != nullptr
                 && savedSlot->getChildByName ("PLUGIN")->getStringAttribute ("name") == "SmartKeysTestSynth"
                 && savedSlot->getChildElementAllSubText ("STATE", {}).isNotEmpty(),
               "el proyecto guarda qué instrumento hay y su sonido (su propio estado)");

        Host reopened;
        if (reopened.load (path) && saved != nullptr)
        {
            mark = synthLogLines();
            reopened.setPluginStateXml (*saved);
            const bool ok = waitForSlot (reopened, "loaded");
            pumpWhileRunning (reopened, 300);
            auto log = readSynthLog (mark);
            std::cout << "     al abrir: " << log.find ("STATE_LOAD") << "   latencia " << reopened.instance().getLatencySamples() << std::endl;
            check (ok && log.has ("CREATED") && log.has ("STATE_LOAD notes=" + String (notes)),
                   "al abrir el proyecto, el instrumento vuelve con su sonido");
            check (reopened.instance().getLatencySamples() == kSynthLatency, "y SmartKeys vuelve a declarar su latencia");
            auto r = reopened.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
            auto tail = reopened.runBlocks (4);
            check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "y suena");
            reopened.run ({ MidiMessage::noteOff (1, 62) });
            reopened.unload();
            check (readSynthLog (mark).has ("DESTROYED"), "cerrar SmartKeys destruye su instrumento");
        }
        else
        {
            check (false, "reabrir el proyecto");
        }
    }

    std::cout << "\n-- H10. UNLOAD: la ranura queda vacía" << std::endl;
    {
        mark = synthLogLines();
        setSlot (host, nullptr);
        const bool empty = waitForSlot (host, "empty");
        pumpWhileRunning (host, 300);
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
        auto tail = host.runBlocks (4);
        check (empty && readSynthLog (mark).has ("DESTROYED"), "el instrumento se destruye");
        check (host.instance().getLatencySamples() == 0, "la latencia vuelve a 0");
        check (jmax (r.outputRms, tail.outputRms) <= 0.0f && r.notesOn (1) == std::set<int> { 62, 65, 69 },
               "silencio, y los acordes siguen saliendo por MIDI");
        host.run ({ MidiMessage::noteOff (1, 62) });
    }

    std::cout << "\n-- H11. Un instrumento que no está instalado no se pierde al guardar" << std::endl;
    {
        PluginDescription ghost;
        ghost.name = ghost.descriptiveName = "Ghost Synth";
        ghost.manufacturerName = "Nadie";
        ghost.pluginFormatName = "VST3";
        ghost.isInstrument = true;
        ghost.fileOrIdentifier = File::getSpecialLocation (File::tempDirectory).getChildFile ("NoExiste").getChildFile ("Ghost.vst3").getFullPathName();
        ghost.uniqueId = 4242;
        const auto ghostState = MemoryBlock ("ABC", 3).toBase64Encoding();   // el formato con el que SmartKeys guarda
        setSlot (host, &ghost, false, ghostState);
        const bool missing = waitForSlot (host, "missing");
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
        auto saved = slotXml (host);
        check (missing, "falta: la ranura lo dice (\"no está instalado\")");
        check (r.outputRms <= 0.0f && r.notesOn (1).size() == 3, "silencio, y los acordes siguen saliendo por MIDI");
        check (saved != nullptr && saved->getChildByName ("PLUGIN") != nullptr
                 && saved->getChildByName ("PLUGIN")->getStringAttribute ("name") == "Ghost Synth"
                 && saved->getChildElementAllSubText ("STATE", {}).trim() == ghostState,
               "al guardar, el proyecto conserva el instrumento que falta y su sonido (para la computadora que lo tenga)");
        host.run ({ MidiMessage::noteOff (1, 62) });
    }

    std::cout << "\n-- H12. Un instrumento que devuelve NaN no rompe la mezcla del DAW" << std::endl;
    {
        loadInSlot (host, synth);
        host.nonFiniteSamples = 0;
        host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });
        auto bad = host.runAt ({ { 10, MidiMessage::controllerEvent (1, 102, 127) } });
        auto after = host.runBlocks (4);
        check (host.nonFiniteSamples == 0, "SmartKeys nunca saca NaN / infinito: ese bloque sale en silencio");
        check (after.outputRms > 1.0e-3f, "y el siguiente bloque vuelve a sonar");
        host.run ({ MidiMessage::noteOff (1, 62) });
        host.settle (4);
    }

   #if SK_COUNT_ALLOCATIONS
    std::cout << "\n-- H13. Con el instrumento cargado: cero asignaciones en el processBlock" << std::endl;
    {
        host.setText ("Strum Speed", "60");
        std::mt19937 rng (21);
        std::set<int> held;
        auto block = [&] (int i)
        {
            const int n = 64 + (int) (rng() % 449);
            std::vector<std::pair<int, MidiMessage>> ev;
            if (rng() % 3 == 0)
            {
                const int note = 48 + (int) (rng() % 30);
                if (held.count (note) > 0) { ev.push_back ({ 0, MidiMessage::noteOff (1, note) }); held.erase (note); }
                else                       { ev.push_back ({ 0, MidiMessage::noteOn (1, note, (uint8) 90) }); held.insert (note); }
            }
            if (i % 300 == 0)
                ev.push_back ({ 0, MidiMessage::controllerEvent (1, 123, 0) });
            host.runSized (n, ev);
        };
        for (int i = 0; i < 400; ++i)
            block (i);
        {
            void* warm[4];
            backtrace (warm, 4);
        }
        gTraceAllocations = SystemStats::getEnvironmentVariable ("SK_ALLOC_TRACE", {}).isNotEmpty();
        gProcessAllocations = 0;
        for (int i = 400; i < 2400; ++i)
        {
            if (i % 250 == 0)
                host.setChoice ("Mode", (i / 250) % 3);
            block (i);
        }
        const long allocs = gProcessAllocations.load();
        std::cout << "     2000 bloques tocando el instrumento (acordes, STRUM, All Notes Off): " << allocs << " asignaciones" << std::endl;
        check (allocs == 0, "el hilo de audio no pide memoria con un instrumento cargado");
        if (gTraceAllocations)
            printAllocationTraces();
        gTraceAllocations = false;
        std::vector<std::pair<int, MidiMessage>> offs;
        for (int note : held)
            offs.push_back ({ 0, MidiMessage::noteOff (1, note) });
        host.runSized (256, offs);
        host.setText ("Strum Speed", "0");
        host.setChoice ("Mode", 0);
        host.settle (4);
    }
   #endif

   #if JUCE_MAC
    if (gSynthAU)
    {
        std::cout << "\n-- H14. Audio Unit: el instrumento de prueba como AU (el formato de FL Studio en Mac)" << std::endl;
        mark = synthLogLines();
        const bool ok = loadInSlot (host, testSynthDescription (true));
        auto r = host.runAt ({ { 50, MidiMessage::noteOn (1, 62, (uint8) 100) } });
        auto tail = host.runBlocks (8);
        auto log = readSynthLog (mark);
        auto savedSlot = slotXml (host);
        std::cout << "     " << (savedSlot != nullptr && savedSlot->getChildByName ("PLUGIN") != nullptr
                                     ? savedSlot->getChildByName ("PLUGIN")->getStringAttribute ("file") : String ("-")) << std::endl;
        check (ok && savedSlot != nullptr && savedSlot->getChildByName ("PLUGIN")->getStringAttribute ("format") == "AudioUnit",
               "el AU se carga en la ranura");
        check (log.notesOn() == std::set<int> { 62, 65, 69 } && sameTiming (midiNoteOnTimes (r), log.noteOnTimes()),
               "el AU recibe el acorde completo en la muestra exacta");
        check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "y suena por SmartKeys");
        host.run ({ MidiMessage::noteOff (1, 62) });
        host.settle (4);
    }
   #endif

    host.unload();
}

//==============================================================================
// Carga protegida: si el programa se cerró mientras se creaba el instrumento, al
// abrir otra vez el proyecto no se vuelve a crear solo (y el proyecto lo conserva).
// Necesita que no quede ningún SmartKeys abierto (el catálogo se lee al crearse).
void testCrashProtection (const String& path)
{
    std::cout << "\n=== 2.0: carga protegida tras un cierre ===" << std::endl;
    const auto synth = testSynthDescription();
    const auto key = synth.pluginFormatName + "|" + synth.fileOrIdentifier + "|" + String (synth.uniqueId);
    const auto userData = File (SystemStats::getEnvironmentVariable ("SMARTKEYS_USER_DATA", {}));
    userData.createDirectory();
    userData.getChildFile ("Cargando plugin.txt").replaceWithText (key);   // "se cerró cargándolo"

    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }
    const int mark = synthLogLines();
    setSlot (host, &synth);
    const bool crashed = waitForSlot (host, "crashed", 3000);
    pumpWhileRunning (host, 300);
    auto log = readSynthLog (mark);
    auto saved = slotXml (host);
    check (crashed && ! log.has ("CREATED"), "el proyecto no vuelve a crear solo el instrumento que cerró el programa");
    check (saved != nullptr && saved->getChildByName ("PLUGIN") != nullptr, "pero lo conserva (se puede reintentar)");
    host.unload();
    check (! userData.getChildFile ("Cargando plugin.txt").existsAsFile()
             || userData.getChildFile ("Cargando plugin.txt").loadFileAsString().trim().isEmpty(),
           "el apunte se limpia");
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

    // 2.0: con el instrumento de prueba en la ranura (se carga, se quita, se cambia y
    // se pone en BYPASS mientras el audio corre)
    const auto synth = testSynthDescription();
    if (synth.fileOrIdentifier.isNotEmpty())
        loadInSlot (host, synth);

    host.setText ("Strum Speed", "70");
    host.setBool ("Mic Voice Controller", true);
    host.eventsOutOfRange = 0;
    host.nonFiniteSamples = 0;

    std::atomic<bool> stop { false }, audioDone { false }, readerDone { false };
    std::atomic<long> blocks { 0 }, stateReads { 0 };
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
        audioDone = true;
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
            if (rng() % 11 == 0)
            {
                // Enciende / apaga el teclado "SmartKeys Chords" (crea y destruye el teclado con el audio corriendo)
                if (auto xml = host.getPluginStateXml())
                {
                    xml->setAttribute ("chordKeyboard", (int) (rng() % 2));
                    host.setPluginStateXml (*xml);
                    ++portToggles;
                }
            }
            if (synth != nullptr && rng() % 5 == 0)
            {
                // La ranura: cargar, quitar, BYPASS, un instrumento que falta, cargar dos veces seguidas
                ++slotChanges;
                switch (rng() % 6)
                {
                    case 0:  setSlot (host, synth); break;
                    case 1:  setSlot (host, nullptr); break;
                    case 2:  setSlotBypass (host, rng() % 2 == 0); break;
                    case 3:  setSlot (host, &ghost); break;
                    case 4:  setSlot (host, synth); setSlot (host, nullptr); setSlot (host, synth); break;
                    default: setSlot (host, synth, rng() % 2 == 0); break;
                }
            }
            if (Time::getMillisecondCounterHiRes() > endMs)
            {
                stopTimer();
                done = true;
            }
        }
        Host& host;
        double endMs;
        int iterations = 0, portToggles = 0, slotChanges = 0;
        bool done = false;
        MemoryBlock previous;
        std::mt19937 rng { 5 };
        const PluginDescription* synth = nullptr;
        PluginDescription ghost;
    };

    Hammer hammer (host, Time::getMillisecondCounterHiRes() + seconds * 1000.0);
    if (synth.fileOrIdentifier.isNotEmpty())
        hammer.synth = &synth;
    hammer.ghost.name = "Ghost Synth";
    hammer.ghost.pluginFormatName = "VST3";
    hammer.ghost.fileOrIdentifier = File::getSpecialLocation (File::tempDirectory).getChildFile ("NoExiste").getChildFile ("Ghost.vst3").getFullPathName();

    // Algunos DAW guardan el estado desde otro hilo: SmartKeys contesta con su copia
    std::thread stateReader ([&]
    {
        while (! stop.load())
        {
            MemoryBlock mb;
            host.getState (mb);
            ++stateReads;
            std::this_thread::sleep_for (std::chrono::milliseconds (9));
        }
        readerDone = true;
    });

    // (runDispatchLoop + stopDispatchLoop dejaría el bucle de mensajes parado para siempre:
    // lo que sigue necesita que SmartKeys reciba sus mensajes)
    hammer.startTimer (7);
    while (! hammer.done)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    stop = true;

    // El host VST3 de JUCE bloquea el hilo de mensajes (MessageManagerLock) para leer el
    // estado desde otro hilo: los mensajes siguen corriendo hasta que los dos hilos terminan
    while (! audioDone.load() || ! readerDone.load())
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    audio.join();
    stateReader.join();

    std::cout << "     " << blocks.load() << " bloques de audio, " << hammer.iterations
              << " rondas de estado/parámetros en el hilo de mensajes (" << hammer.portToggles
              << " veces encendido/apagado el teclado SmartKeys Chords, " << hammer.slotChanges
              << " cambios de la ranura de instrumento), " << stateReads.load() << " estados leídos desde otro hilo" << std::endl;
    check (blocks.load() > 200 && hammer.iterations > 20, "los dos hilos trabajaron a la vez");
    check (host.eventsOutOfRange == 0, "con todo a la vez, ningún evento MIDI fuera de su bloque");
    check (host.nonFiniteSamples == 0, "y nunca sale NaN / infinito");
    if (synth.fileOrIdentifier.isNotEmpty())
    {
        check (hammer.slotChanges > 10, "el instrumento se cargó, quitó y cambió con el audio corriendo");

        // Lo que el martilleo dejó al azar (GAIN hasta -39 dB, una sola nota, STRUM, MIC VOICE): como al principio
        host.setText ("Master Gain", "-3");
        host.setChoice ("Mode", 0);
        host.setText ("Strum Speed", "0");
        host.setBool ("Mic Voice Controller", false);
        setSlot (host, &synth);
        check (waitForSlot (host, "loaded"), "y al final se carga y suena como siempre");
        auto r = host.run ({ MidiMessage::noteOn (1, 60, (uint8) 100) });
        auto tail = host.runBlocks (6);
        check (jmax (r.outputRms, tail.outputRms) > 1.0e-3f, "suena");
        host.run ({ MidiMessage::noteOff (1, 60) });
    }
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

    const auto synth = testSynthDescription();
    if (synth.fileOrIdentifier.isEmpty() || ! loadInSlot (host, synth))
        check (false, "el instrumento de prueba se carga en la ranura");

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
    check (sing.outputRms > 1.0e-3f, "y el instrumento cargado lo toca");
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
//==============================================================================
// Teclado MIDI virtual "SmartKeys Chords"
//==============================================================================
struct PortEvent
{
    double ms = 0.0;   // reloj de alta resolución (Time::getMillisecondCounterHiRes)
    int status = 0, data1 = 0, data2 = 0;

    bool isNoteOn() const  { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

// Lo que sale por el teclado. macOS: se abre como entrada MIDI (CoreMIDI de
// verdad, como lo ve FL Studio). Linux: el archivo que escribe el teclado.
class PortTap
   #if JUCE_MAC
    : private MidiInputCallback
   #endif
{
public:
    explicit PortTap (String fileForLinux) : file (std::move (fileForLinux)) {}

    ~PortTap()
    {
       #if JUCE_MAC
        if (input != nullptr)
            input->stop();
       #endif
    }

    bool connect (const String& deviceName)
    {
       #if JUCE_MAC
        if (input != nullptr)
            return true;
        for (auto& d : MidiInput::getAvailableDevices())
        {
            if (d.name == deviceName)
            {
                input = MidiInput::openDevice (d.identifier, this);
                if (input != nullptr)
                    input->start();
                return input != nullptr;
            }
        }
        return false;
       #else
        ignoreUnused (deviceName);
        return File (file).existsAsFile();
       #endif
    }

    bool deviceListed (const String& deviceName) const
    {
       #if JUCE_MAC
        for (auto& d : MidiInput::getAvailableDevices())
            if (d.name == deviceName)
                return true;
        return false;
       #else
        ignoreUnused (deviceName);
        return true;
       #endif
    }

    // Lo que salió desde la última llamada
    std::vector<PortEvent> poll()
    {
       #if JUCE_MAC
        const ScopedLock sl (lock);
        auto out = std::move (received);
        received.clear();
        return out;
       #else
        std::vector<PortEvent> out;
        std::FILE* f = std::fopen (file.toRawUTF8(), "r");
        if (f == nullptr)
            return out;
        std::fseek (f, offset, SEEK_SET);
        char line[160];
        while (std::fgets (line, sizeof (line), f) != nullptr)
        {
            const auto len = std::strlen (line);
            if (len == 0 || line[len - 1] != '\n')
                break;   // línea a medio escribir: la próxima vez
            offset += (long) len;
            PortEvent e;
            if (std::sscanf (line, "%lf %d %d %d", &e.ms, &e.status, &e.data1, &e.data2) == 4)
                out.push_back (e);
        }
        std::fclose (f);
        return out;
       #endif
    }

private:
    String file;
    long offset = 0;

   #if JUCE_MAC
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& m) override
    {
        if (m.getRawDataSize() != 3)
            return;
        const auto* d = m.getRawData();
        // La hora del paquete CoreMIDI = cuándo lo envió el teclado (sin el retraso
        // de entrega del servidor MIDI, que en una máquina virtual varía mucho)
        const double sentMs = m.getTimeStamp() > 0.0 ? m.getTimeStamp() * 1000.0 : Time::getMillisecondCounterHiRes();
        const ScopedLock sl (lock);
        received.push_back ({ sentMs, d[0], d[1], d[2] });
    }

    std::unique_ptr<MidiInput> input;
    CriticalSection lock;
    std::vector<PortEvent> received;
   #endif
};

struct UserEvent
{
    double atMs;   // desde el inicio del escenario
    MidiMessage msg;
};

// Voz por la entrada del plugin (sidechain) durante [fromMs, toMs) del escenario
struct InputTone
{
    double fromMs = 0.0, toMs = 0.0, hz = 0.0;
    float amplitude = 0.0f;
};

struct PortScenario
{
    BlockResult out;                 // salida MIDI del plugin (marcas en muestras, desde el inicio)
    std::vector<PortEvent> port;     // lo que salió por el teclado
    double t0 = 0.0;
    int echoes = 0;                  // eventos devueltos al plugin (bucle simulado)
};

// Espera precisa hasta 'targetMs' (duerme y, el último milisegundo y medio, cede el procesador)
void waitUntilMs (double targetMs)
{
    for (;;)
    {
        const double remaining = targetMs - Time::getMillisecondCounterHiRes();
        if (remaining <= 0.0)
            return;
        if (remaining > 2.0)
            std::this_thread::sleep_for (std::chrono::microseconds ((int64) ((remaining - 1.5) * 1000.0)));
        else
            std::this_thread::yield();
    }
}

// Hilo de "audio" del DAW de prueba: como en un DAW de verdad, el processBlock va
// en su propio hilo de tiempo real, a su ritmo, y el hilo de mensajes (el
// temporizador del plugin, que crea el teclado) corre aparte.
class AudioThread : public Thread
{
public:
    explicit AudioThread (std::function<void()> fn) : Thread ("SmartKeysHostTests audio"), body (std::move (fn)) {}

    void run() override
    {
        body();
        finished = true;
    }

    std::atomic<bool> finished { false };

private:
    std::function<void()> body;
};

// Escenario a TIEMPO REAL: bloques de 512 muestras a su ritmo en el hilo de audio,
// el hilo de mensajes corre a la vez (el Timer del plugin crea el teclado) y, con
// 'loopback', todo lo que sale por el teclado vuelve a entrar por la entrada MIDI
// del plugin en el bloque siguiente, como en FL Studio con el teclado devuelto.
PortScenario runPortScenario (Host& host, PortTap& tap, double seconds, std::vector<UserEvent> user, bool loopback,
                              int subBlocks = 1, Host* second = nullptr, std::vector<UserEvent> secondUser = {},
                              InputTone tone = {})
{
    std::stable_sort (user.begin(), user.end(), [] (const UserEvent& a, const UserEvent& b) { return a.atMs < b.atMs; });
    std::stable_sort (secondUser.begin(), secondUser.end(), [] (const UserEvent& a, const UserEvent& b) { return a.atMs < b.atMs; });

    PortScenario sc;
    const double blockMs = 1000.0 * Host::blockSize / Host::sampleRate;
    const int numBlocks = (int) std::floor (seconds * 1000.0 / blockMs) + 1;
    double maxBlockLateMs = 0.0;

    AudioThread audio ([&]
    {
        size_t nextUser = 0, nextSecond = 0;
        for (int k = 0; k < numBlocks; ++k)
        {
            const double blockStart = sc.t0 + (double) k * blockMs;
            waitUntilMs (blockStart);
            maxBlockLateMs = jmax (maxBlockLateMs, Time::getMillisecondCounterHiRes() - blockStart);

            std::vector<std::pair<int, MidiMessage>> ev;
            for (auto& e : tap.poll())
            {
                sc.port.push_back (e);
                if (loopback)
                {
                    ev.push_back ({ 0, MidiMessage (e.status, e.data1, e.data2) });
                    ++sc.echoes;
                }
            }

            while (nextUser < user.size() && user[nextUser].atMs < (double) (k + 1) * blockMs)
            {
                const int pos = jlimit (0, Host::blockSize - 1,
                                        (int) ((user[nextUser].atMs - (double) k * blockMs) * Host::sampleRate / 1000.0));
                ev.push_back ({ pos, user[nextUser].msg });
                ++nextUser;
            }

            if (second != nullptr)
            {
                // Otra instancia de SmartKeys en el mismo DAW, con sus propias teclas
                std::vector<std::pair<int, MidiMessage>> ev2;
                while (nextSecond < secondUser.size() && secondUser[nextSecond].atMs < (double) (k + 1) * blockMs)
                {
                    const int pos = jlimit (0, Host::blockSize - 1,
                                            (int) ((secondUser[nextSecond].atMs - (double) k * blockMs) * Host::sampleRate / 1000.0));
                    ev2.push_back ({ pos, secondUser[nextSecond].msg });
                    ++nextSecond;
                }
                second->runAt (ev2);
            }

            const double blockFromMs = (double) k * blockMs;
            if (tone.hz > 0.0 && blockFromMs >= tone.fromMs && blockFromMs < tone.toMs)
            {
                auto r = host.runSized (Host::blockSize, ev, tone.hz, tone.amplitude);   // se canta en este bloque
                sc.out.midi.insert (sc.out.midi.end(), r.midi.begin(), r.midi.end());
                sc.out.outputRms = jmax (sc.out.outputRms, r.outputRms);
            }
            else if (subBlocks <= 1)
            {
                auto r = host.runAt (ev);
                sc.out.midi.insert (sc.out.midi.end(), r.midi.begin(), r.midi.end());
                sc.out.outputRms = jmax (sc.out.outputRms, r.outputRms);
            }
            else
            {
                // Como FL Studio: el búfer se procesa en trozos seguidos, sin esperar entre ellos
                const int sub = Host::blockSize / subBlocks;
                for (int b = 0; b < subBlocks; ++b)
                {
                    std::vector<std::pair<int, MidiMessage>> part;
                    for (auto& e : ev)
                        if (e.first / sub == b || (b == subBlocks - 1 && e.first >= sub * subBlocks))
                            part.push_back ({ e.first - b * sub, e.second });
                    auto r = host.runSized (b == subBlocks - 1 ? Host::blockSize - sub * b : sub, part);
                    sc.out.midi.insert (sc.out.midi.end(), r.midi.begin(), r.midi.end());
                    sc.out.outputRms = jmax (sc.out.outputRms, r.outputRms);
                }
            }
        }

        // El audio de un DAW es continuo: el escenario termina cuando acaba su último bloque
        waitUntilMs (sc.t0 + (double) numBlocks * blockMs);
    });

    sc.t0 = Time::getMillisecondCounterHiRes() + 2.0;
    if (! audio.startRealtimeThread (Thread::RealtimeOptions{}.withPeriodMs (blockMs).withMaximumProcessingTimeMs (blockMs * 0.8)))
        audio.startThread (Thread::Priority::highest);

    while (! audio.finished.load())
        MessageManager::getInstance()->runDispatchLoopUntil (5);
    audio.stopThread (2000);

    for (auto& e : tap.poll())
        sc.port.push_back (e);

    if (SystemStats::getEnvironmentVariable ("SMARTKEYS_PORT_DEBUG", {}).isNotEmpty() && (! sc.out.midi.empty() || ! sc.port.empty()))
        std::cout << "     [hilo de audio de la prueba] bloque más atrasado: " << String (maxBlockLateMs, 2) << " ms" << std::endl;

    if (SystemStats::getEnvironmentVariable ("SK_PORT_DEBUG", {}).isNotEmpty() && (! sc.out.midi.empty() || ! sc.port.empty()))
    {
        std::cout << "     [debug] salida MIDI:";
        for (auto& m : sc.out.midi)
            if (m.isNoteOnOrOff())
                std::cout << " " << (m.isNoteOn() ? "+" : "-") << m.getNoteNumber();
        std::cout << std::endl << "     [debug] teclado (ms desde el inicio):";
        for (auto& e : sc.port)
            std::cout << " " << (e.isNoteOn() ? "+" : "-") << e.data1 << "@" << roundToInt (e.ms - sc.t0);
        std::cout << std::endl;
    }
    return sc;
}

std::map<int, int> countNoteOns (const std::vector<MidiMessage>& midi)
{
    std::map<int, int> c;
    for (auto& m : midi)
        if (m.isNoteOn())
            ++c[m.getNoteNumber()];
    return c;
}

std::map<int, int> countPortNoteOns (const std::vector<PortEvent>& port)
{
    std::map<int, int> c;
    for (auto& e : port)
        if (e.isNoteOn())
            ++c[e.data1];
    return c;
}

// Notas que quedarían colgadas (NoteOn - NoteOff por nota)
int hangingNotes (const std::vector<MidiMessage>& midi)
{
    std::map<int, int> balance;
    for (auto& m : midi)
    {
        if (m.isNoteOn())  ++balance[m.getNoteNumber()];
        if (m.isNoteOff()) --balance[m.getNoteNumber()];
    }
    int hanging = 0;
    for (auto& [note, b] : balance)
        hanging += std::abs (b);
    return hanging;
}

int hangingPortNotes (const std::vector<PortEvent>& port)
{
    std::map<int, int> balance;
    for (auto& e : port)
    {
        if (e.isNoteOn())  ++balance[e.data1];
        if (e.isNoteOff()) --balance[e.data1];
    }
    int hanging = 0;
    for (auto& [note, b] : balance)
        hanging += std::abs (b);
    return hanging;
}

// Retardo de la nota 'index' del STRUM (StrumScheduler: t_i = i · base · (1 + 0.15 · i))
double strumMs (int index, double baseMs)
{
    const double i = (double) index;
    return i * baseMs * (1.0 + 0.15 * i);
}

String describeCounts (const std::map<int, int>& c)
{
    String s;
    for (auto& [note, n] : c)
        s << (s.isEmpty() ? "" : " ") << MidiMessage::getMidiNoteName (note, true, true, 4) << "x" << n;
    return "{" + s + "}";
}

void testChordPort (const String& path)
{
    std::cout << "\n=== Teclado MIDI virtual \"SmartKeys Chords\" (lo que FL Studio graba en el Piano Roll) ===" << std::endl;

   #if JUCE_WINDOWS
    std::cout << "  (Windows: un plugin no puede crear teclados MIDI virtuales; no aplica)" << std::endl;
    ignoreUnused (path);
   #else
    const auto tapFile = File::getSpecialLocation (File::tempDirectory)
                             .getChildFile ("smartkeys_chords_" + String (Time::currentTimeMillis()) + ".txt");
   #if JUCE_LINUX
    setenv ("SMARTKEYS_TEST_CHORD_PORT", tapFile.getFullPathName().toRawUTF8(), 1);
   #endif
    const String deviceName ("SmartKeys Chords");
    const double blockMs = 1000.0 * Host::blockSize / Host::sampleRate;

    auto tap = std::make_unique<PortTap> (tapFile.getFullPathName());
    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    // Fa mayor, SMART CHORDS, tríadas, sin STRUM
    host.setChoice ("Key", 5);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Chord Type", 0);
    host.setBool ("Mic Voice Controller", false);
    host.setText ("Strum Speed", "0");
    host.setChoice ("Strum Direction", 0);

    auto setChordKeyboard = [&host] (bool on)
    {
        if (auto xml = host.getPluginStateXml())
        {
            xml->setAttribute ("chordKeyboard", on ? 1 : 0);
            host.setPluginStateXml (*xml);
        }
    };

    std::cout << "\n-- P0. Fuera de FL Studio empieza apagado (Logic / Ableton grabarían ese teclado en la pista)" << std::endl;
    {
        runPortScenario (host, *tap, 0.8, {}, false);
        const bool appeared = tap->connect (deviceName);
        auto state = host.getPluginStateXml();
        check (! appeared && state != nullptr && state->getIntAttribute ("chordKeyboard", -1) == 0,
               "en un DAW que no es FL Studio, el teclado no se crea (y el proyecto lo guarda apagado)");
    }

    std::cout << "\n-- P1. Encendido (clic en la etiqueta o proyecto guardado así): aparece en cuanto el plugin suena" << std::endl;
    setChordKeyboard (true);
    bool connected = false;
    double waitedMs = 0.0;
    const double startWait = Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 60 && ! connected; ++i)
    {
        runPortScenario (host, *tap, 0.1, {}, false);
        connected = tap->connect (deviceName);
    }
    waitedMs = Time::getMillisecondCounterHiRes() - startWait;
    std::cout << "     \"" << deviceName << "\" disponible a los " << roundToInt (waitedMs) << " ms" << std::endl;
    check (connected, "el plugin crea el teclado MIDI \"SmartKeys Chords\" (FL Studio lo ve como un teclado)");
    {
        auto state = host.getPluginStateXml();
        check (state != nullptr && state->getIntAttribute ("chordKeyboard", -1) == 1, "el proyecto guarda el teclado encendido");
    }
    if (! connected)
    {
        host.unload();
        return;
    }
    runPortScenario (host, *tap, 0.3, {}, false);   // descarta lo anterior

    std::cout << "\n-- P2. Una tecla -> el acorde COMPLETO sale por el teclado, todas las notas a la vez" << std::endl;
    {
        auto sc = runPortScenario (host, *tap, 0.8, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                      { 400.0, MidiMessage::noteOff (1, 54) } }, false);
        std::set<double> outSamples;
        for (auto& m : sc.out.midi)
            if (m.isNoteOn())
                outSamples.insert (m.getTimeStamp());
        std::map<int, double> onAt;
        std::set<int> offs;
        bool channelOne = true;
        for (auto& e : sc.port)
        {
            if (e.isNoteOn())  onAt[e.data1] = e.ms;
            if (e.isNoteOff()) offs.insert (e.data1);
            channelOne = channelOne && (e.status & 0x0F) == 0;
        }
        std::set<int> onSet;
        double first = 1.0e300, last = -1.0e300;
        for (auto& [n, t] : onAt)
        {
            onSet.insert (n);
            first = jmin (first, t);
            last = jmax (last, t);
        }
        const double latency = first - (sc.t0 + 100.0);
        std::cout << "     F#3 -> salida MIDI " << describe (sc.out.notesOn (1)) << "; teclado " << describe (onSet)
                  << " en " << String (last - first, 2) << " ms, " << String (latency, 1) << " ms después de la tecla" << std::endl;
        check (sc.out.notesOn (1) == std::set<int> { 53, 57, 60 } && outSamples.size() == 1,
               "salida MIDI del plugin: F#3 en Fa mayor -> {F3 A3 C4} en la misma muestra");
        check (onSet == std::set<int> { 53, 57, 60 } && channelOne,
               "por el teclado salen las 3 notas del acorde (no sólo la tecla), canal 1");
        check (last - first <= 2.0, "las 3 notas a la vez (±2 ms): FL las dibuja apiladas");
        check (latency >= -1.0 && latency <= 3.0 * blockMs + 15.0, "con la latencia de ~1 bloque de audio");
        check (offs == std::set<int> { 53, 57, 60 } && hangingPortNotes (sc.port) == 0, "al soltar, los 3 NoteOff");
    }

    std::cout << "\n-- P3. STRUM 41 ms DESC: el mismo desfase en la salida MIDI y en el teclado" << std::endl;
    {
        host.setText ("Strum Speed", "41");
        host.setChoice ("Strum Direction", 1);
        runPortScenario (host, *tap, 0.1, {}, false);
        auto sc = runPortScenario (host, *tap, 1.1, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                      { 800.0, MidiMessage::noteOff (1, 54) } }, false);
        std::map<int, double> outAt, portAt;
        for (auto& m : sc.out.midi)
            if (m.isNoteOn() && outAt.count (m.getNoteNumber()) == 0)
                outAt[m.getNoteNumber()] = m.getTimeStamp();
        for (auto& e : sc.port)
            if (e.isNoteOn() && portAt.count (e.data1) == 0)
                portAt[e.data1] = e.ms;

        const double d1 = strumMs (1, 41.0), d2 = strumMs (2, 41.0);
        const bool outOk = outAt.size() == 3
                           && std::abs ((outAt[57] - outAt[60]) - d1 * Host::sampleRate / 1000.0) <= 2.0
                           && std::abs ((outAt[53] - outAt[60]) - d2 * Host::sampleRate / 1000.0) <= 2.0;
        const bool portOk = portAt.size() == 3
                            && std::abs ((portAt[57] - portAt[60]) - d1) <= 5.0
                            && std::abs ((portAt[53] - portAt[60]) - d2) <= 5.0;
        std::cout << "     teclado: C4 +0, A3 +" << String (portAt[57] - portAt[60], 1) << " ms, F3 +"
                  << String (portAt[53] - portAt[60], 1) << " ms (esperado +" << String (d1, 1) << " / +" << String (d2, 1) << ")" << std::endl;
        check (outOk, "salida MIDI: C4 -> A3 -> F3 con el desfase exacto (±2 muestras)");
        check (portOk, "teclado: el mismo desfase (±5 ms): FL graba el acorde rasgueado");
        check (hangingPortNotes (sc.port) == 0, "al soltar, ninguna nota colgada");

        // FL Studio parte su búfer y procesa los trozos de golpe: el desfase se conserva igual
        auto fl = runPortScenario (host, *tap, 1.1, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                      { 800.0, MidiMessage::noteOff (1, 54) } }, false, 4);
        std::map<int, double> flAt;
        for (auto& e : fl.port)
            if (e.isNoteOn() && flAt.count (e.data1) == 0)
                flAt[e.data1] = e.ms;
        std::cout << "     en 4 trozos de 128 muestras seguidos: A3 +" << String (flAt[57] - flAt[60], 1) << " ms, F3 +"
                  << String (flAt[53] - flAt[60], 1) << " ms" << std::endl;
        check (flAt.size() == 3 && std::abs ((flAt[57] - flAt[60]) - d1) <= 5.0 && std::abs ((flAt[53] - flAt[60]) - d2) <= 5.0
                 && hangingPortNotes (fl.port) == 0,
               "con el búfer partido en trozos (como FL Studio), el teclado conserva el desfase (±5 ms)");
        host.setText ("Strum Speed", "0");
        host.setChoice ("Strum Direction", 0);
    }

    std::cout << "\n-- P4. Sin bucle (FLEX enganchado al teclado): ninguna tecla se pierde" << std::endl;
    {
        std::vector<UserEvent> ev { { 50.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 150.0, MidiMessage::noteOff (1, 54) } };
        for (int k = 0; k < 8; ++k)
        {
            ev.push_back ({ 600.0 + 60.0 * k, MidiMessage::noteOn (1, 53, (uint8) 100) });
            ev.push_back ({ 630.0 + 60.0 * k, MidiMessage::noteOff (1, 53) });
        }
        ev.push_back ({ 1200.0, MidiMessage::noteOn (1, 53, (uint8) 100) });
        ev.push_back ({ 1220.0, MidiMessage::noteOn (1, 57, (uint8) 100) });
        ev.push_back ({ 1500.0, MidiMessage::noteOff (1, 53) });
        ev.push_back ({ 1500.0, MidiMessage::noteOff (1, 57) });
        auto sc = runPortScenario (host, *tap, 1.9, ev, false);
        auto counts = countNoteOns (sc.out.midi);
        std::cout << "     NoteOn de salida: " << describeCounts (counts) << std::endl;
        check (counts[53] == 10, "repetir F3 cada 60 ms: el acorde sale las 8 veces");
        check (counts[64] == 1, "A3 20 ms después de F3 (nota del acorde anterior) también dispara su acorde (La menor)");
        check (hangingNotes (sc.out.midi) == 0 && hangingPortNotes (sc.port) == 0, "nada queda colgado");
    }

    std::cout << "\n-- P5. Bucle: FL devuelve el teclado a SmartKeys -> los ecos no disparan acordes" << std::endl;
    {
        // Como FL al reiniciar el audio: SmartKeys no sabe nada todavía del bucle
        host.instance().releaseResources();
        host.instance().prepareToPlay (Host::sampleRate, Host::blockSize);
        runPortScenario (host, *tap, 0.3, {}, false);

        std::vector<UserEvent> ev {
            { 100.0,  MidiMessage::noteOn (1, 54, (uint8) 100) }, { 700.0,  MidiMessage::noteOff (1, 54) },    // F
            { 1000.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 1200.0, MidiMessage::noteOn (1, 57, (uint8) 90) },
            { 1400.0, MidiMessage::noteOff (1, 54) },             { 1700.0, MidiMessage::noteOff (1, 57) } };  // legato F -> Am
        for (int k = 0; k < 6; ++k)
        {
            ev.push_back ({ 1900.0 + 80.0 * k, MidiMessage::noteOn (1, 53, (uint8) 100) });
            ev.push_back ({ 1940.0 + 80.0 * k, MidiMessage::noteOff (1, 53) });
        }
        auto sc = runPortScenario (host, *tap, 3.0, ev, true);
        auto counts = countNoteOns (sc.out.midi);
        std::cout << "     " << sc.echoes << " ecos devueltos; NoteOn de salida: " << describeCounts (counts) << std::endl;
        check (counts == std::map<int, int> { { 53, 8 }, { 57, 8 }, { 60, 8 }, { 64, 1 } },
               "con el bucle: sólo los acordes de las teclas pulsadas (F, legato F -> Am, 6 repeticiones), ni uno más");
        check (sc.echoes >= 40, "los ecos llegaron de verdad (NoteOn y NoteOff de cada acorde)");
        check (hangingNotes (sc.out.midi) == 0 && hangingPortNotes (sc.port) == 0, "y nada queda colgado");

        host.setText ("Strum Speed", "41");
        host.setChoice ("Strum Direction", 1);
        auto st = runPortScenario (host, *tap, 1.2, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                      { 700.0, MidiMessage::noteOff (1, 54) } }, true);
        auto sCounts = countNoteOns (st.out.midi);
        std::cout << "     STRUM 41 ms DESC con el bucle: " << describeCounts (sCounts) << std::endl;
        check (sCounts == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } } && hangingNotes (st.out.midi) == 0
                 && hangingPortNotes (st.port) == 0,
               "con STRUM, los ecos que vuelven de uno en uno tampoco disparan nada");
        host.setText ("Strum Speed", "0");
        host.setChoice ("Strum Direction", 0);
        runPortScenario (host, *tap, 0.3, {}, true);

       #if SK_COUNT_ALLOCATIONS
        // El processBlock real con el teclado activo, sus ecos y el STRUM: 0 asignaciones
        host.setText ("Strum Speed", "30");
        std::vector<UserEvent> busy;
        for (int k = 0; k < 6; ++k)
        {
            busy.push_back ({ 50.0 + 160.0 * k, MidiMessage::noteOn (1, 53 + 2 * k, (uint8) 100) });
            busy.push_back ({ 190.0 + 160.0 * k, MidiMessage::noteOff (1, 53 + 2 * k) });
        }
        runPortScenario (host, *tap, 1.2, busy, true);   // calentamiento
        gProcessAllocations = 0;
        auto counted = runPortScenario (host, *tap, 1.2, busy, true);
        const long allocs = gProcessAllocations.load();
        std::cout << "     " << counted.echoes << " ecos con STRUM: " << allocs << " asignaciones en el hilo de audio" << std::endl;
        check (allocs == 0 && counted.echoes > 0, "con el teclado activo y sus ecos, processBlock no pide memoria");
        host.setText ("Strum Speed", "0");
        runPortScenario (host, *tap, 0.5, {}, true);
       #endif
    }

    std::cout << "\n-- P6. El bucle aparece de repente (SmartKeys creía que no había)" << std::endl;
    {
        for (int strum : { 0, 41 })
        {
            host.setText ("Strum Speed", String (strum));
            host.setChoice ("Strum Direction", 1);
            // Sin bucle: SmartKeys aprende que lo que sale no vuelve
            runPortScenario (host, *tap, 0.9, { { 50.0, MidiMessage::noteOn (1, 57, (uint8) 100) },
                                                { 500.0, MidiMessage::noteOff (1, 57) } }, false);
            // Y de pronto, el bucle
            auto sc = runPortScenario (host, *tap, 2.0, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                          { 700.0, MidiMessage::noteOff (1, 54) } }, true);
            auto counts = countNoteOns (sc.out.midi);
            int extra = 0;
            for (auto& [note, n] : counts)
                extra += (note == 53 || note == 57 || note == 60) ? n - 1 : n;
            std::cout << "     STRUM " << strum << " ms: " << describeCounts (counts) << "  (" << extra << " notas de más)" << std::endl;
            if (strum == 0)
                check (extra == 0, "sin STRUM, el acorde que vuelve entero se reconoce al instante");
            check (hangingNotes (sc.out.midi) == 0 && hangingPortNotes (sc.port) == 0,
                   strum == 0 ? "y nada queda colgado" : "con STRUM: tras soltar la tecla no queda ninguna nota colgada");
        }
        host.setText ("Strum Speed", "0");
        host.setChoice ("Strum Direction", 0);
        host.instance().releaseResources();
        host.instance().prepareToPlay (Host::sampleRate, Host::blockSize);
        runPortScenario (host, *tap, 0.3, {}, false);
    }

    std::cout << "\n-- P8. Grabación en bucle: lo que FL repite en cada vuelta no sale otra vez (sin notas dobles)" << std::endl;
    {
        // FL en modo patrón: un compás de 4 negras a 120 BPM (2 s) que se repite.
        // Vuelta 1: F#3 en vivo en la negra 1. Vuelta 2: FL repite esa tecla (ya grabada en el
        // patrón de SmartKeys) en la misma negra, y se toca una tecla nueva (A3 en la negra 2,4).
        const std::vector<UserEvent> twoPasses { { 500.0,  MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                 { 900.0,  MidiMessage::noteOff (1, 54) },
                                                 { 2500.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                 { 2900.0, MidiMessage::noteOff (1, 54) },
                                                 { 3200.0, MidiMessage::noteOn (1, 57, (uint8) 100) },
                                                 { 3500.0, MidiMessage::noteOff (1, 57) } };
        const std::map<int, int> outBoth { { 53, 2 }, { 57, 3 }, { 60, 3 }, { 64, 1 } };
        const std::map<int, int> portOnce { { 53, 1 }, { 57, 2 }, { 60, 2 }, { 64, 1 } };
        auto stopTransport = [&]
        {
            host.transport.playing = host.transport.recording = false;
            runPortScenario (host, *tap, 0.2, {}, false);
            host.transport.ppq = 0.0;
        };

        // a) Grabando
        host.transport.bpm = 120.0;
        host.transport.loopBeats = 4.0;
        host.transport.ppq = 0.0;
        host.transport.playing = true;
        host.transport.recording = true;
        auto sc = runPortScenario (host, *tap, 4.3, twoPasses, false);
        auto outCounts = countNoteOns (sc.out.midi);
        auto portCounts = countPortNoteOns (sc.port);
        std::cout << "     salida MIDI: " << describeCounts (outCounts) << "   teclado: " << describeCounts (portCounts) << std::endl;
        check (outCounts == outBoth,
               "la salida MIDI y el instrumento tocan las dos vueltas (F, F otra vez y La menor)");
        check (portCounts == portOnce && hangingPortNotes (sc.port) == 0,
               "por el teclado, el acorde que FL repite en la vuelta 2 no sale otra vez; la tecla nueva sí (nada se graba doble)");

        // b) Sin grabar (tocando en vivo sobre el bucle), cada vuelta sale entera por el teclado
        stopTransport();
        host.transport.playing = true;
        auto jam = runPortScenario (host, *tap, 4.3, twoPasses, false);
        auto jamPort = countPortNoteOns (jam.port);
        std::cout << "     sin grabar: salida MIDI " << describeCounts (countNoteOns (jam.out.midi))
                  << "   teclado " << describeCounts (jamPort) << std::endl;
        check (countNoteOns (jam.out.midi) == outBoth && jamPort == outBoth && hangingPortNotes (jam.port) == 0,
               "sin grabar (tocando en vivo sobre el bucle), lo que se repite en cada vuelta sale entero por el teclado");

        // c) En una toma nueva, la primera vuelta sale entera
        stopTransport();
        host.transport.playing = true;
        host.transport.recording = true;
        auto take2 = runPortScenario (host, *tap, 1.2, { { 500.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                         { 900.0, MidiMessage::noteOff (1, 54) } }, false);
        check (countPortNoteOns (take2.port) == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } },
               "en una toma nueva, la primera vuelta vuelve a salir entera");

        // d) Un acorde en la negra 1 (el primero del compás): la vuelta siguiente llega en
        //    un bloque que empezó antes del final del bucle (este "DAW" no corta el bloque
        //    ahí). Sin puntos de bucle, SmartKeys aprende el final en la primera vuelta.
        const std::vector<UserEvent> downbeats { {    0.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, {  400.0, MidiMessage::noteOff (1, 54) },
                                                 { 2000.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 2400.0, MidiMessage::noteOff (1, 54) },
                                                 { 4000.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 4400.0, MidiMessage::noteOff (1, 54) },
                                                 { 6000.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 6400.0, MidiMessage::noteOff (1, 54) } };
        stopTransport();
        host.transport.playing = host.transport.recording = true;
        auto seam = runPortScenario (host, *tap, 6.6, downbeats, false);
        const auto seamPort = countPortNoteOns (seam.port);
        std::cout << "     negra 1, 4 vueltas, sin puntos de bucle: salida MIDI " << describeCounts (countNoteOns (seam.out.midi))
                  << "   teclado " << describeCounts (seamPort) << std::endl;
        check (countNoteOns (seam.out.midi) == std::map<int, int> { { 53, 4 }, { 57, 4 }, { 60, 4 } }
                 && seamPort.size() == 3 && seamPort.at (53) <= 2 && seamPort.at (57) == seamPort.at (53) && seamPort.at (60) == seamPort.at (53)
                 && hangingPortNotes (seam.port) == 0,
               "acorde en la negra 1: tras aprender el final del bucle (1.ª vuelta), las vueltas 3 y 4 no lo repiten aunque el bloque cruce el final");

        // e) Con los puntos de bucle del DAW, ni siquiera la primera vuelta lo repite
        stopTransport();
        host.transport.reportLoop = true;
        host.transport.playing = host.transport.recording = true;
        auto seamLoop = runPortScenario (host, *tap, 4.6, { downbeats.begin(), downbeats.begin() + 6 }, false);
        const auto seamLoopPort = countPortNoteOns (seamLoop.port);
        std::cout << "     negra 1, 3 vueltas, con puntos de bucle: teclado " << describeCounts (seamLoopPort) << std::endl;
        check (seamLoopPort == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } } && hangingPortNotes (seamLoop.port) == 0,
               "con los puntos de bucle del DAW, el acorde de la negra 1 sale una sola vez en todas las vueltas");
        host.transport.reportLoop = false;

        // f) Sin transporte (tocando en vivo con FL parado), cada tecla sale siempre
        stopTransport();
        auto live = runPortScenario (host, *tap, 1.2, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                        { 300.0, MidiMessage::noteOff (1, 54) },
                                                        { 600.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                        { 800.0, MidiMessage::noteOff (1, 54) } }, false);
        check (countPortNoteOns (live.port) == std::map<int, int> { { 53, 2 }, { 57, 2 }, { 60, 2 } },
               "con FL parado, repetir una tecla la manda otra vez por el teclado");

        host.transport.loopBeats = 0.0;
        stopTransport();
    }

    std::cout << "\n-- P9. Dos SmartKeys a la vez (dos canales seleccionados en FL): cada nota sale una sola vez" << std::endl;
    {
        Host other;
        if (other.load (path))
        {
            other.setChoice ("Key", 5);
            other.setChoice ("Scale", 0);
            other.setChoice ("Mode", 0);
            other.setChoice ("Chord Type", 0);
            other.setBool ("Mic Voice Controller", false);
            other.setText ("Strum Speed", "0");
            if (auto xml = other.getPluginStateXml())
            {
                xml->setAttribute ("chordKeyboard", 1);
                other.setPluginStateXml (*xml);
            }
            runPortScenario (host, *tap, 0.6, {}, false, 1, &other, {});

            // Las dos instancias tocan F#3 a la vez; la segunda la suelta más tarde
            auto sc = runPortScenario (host, *tap, 1.2,
                                       { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 400.0, MidiMessage::noteOff (1, 54) } },
                                       false, 1, &other,
                                       { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) }, { 800.0, MidiMessage::noteOff (1, 54) } });
            auto portCounts = countPortNoteOns (sc.port);
            double firstOff = 1.0e300;
            for (auto& e : sc.port)
                if (e.isNoteOff())
                    firstOff = jmin (firstOff, e.ms - sc.t0);
            std::cout << "     teclado: " << describeCounts (portCounts) << ", primer NoteOff a los "
                      << roundToInt (firstOff) << " ms" << std::endl;
            check (portCounts == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } },
                   "el acorde que tocan las dos instancias sale una sola vez (FL no recibe notas dobles)");
            check (firstOff >= 790.0 && hangingPortNotes (sc.port) == 0,
                   "y se apaga cuando lo suelta la última que lo tocaba");
            other.unload();
            MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        else
        {
            check (false, "cargar una segunda instancia");
        }
    }

    std::cout << "\n-- P10. ACORDES AL PIANO ROLL: FL graba en el Piano Roll de SmartKeys el acorde completo, no la tecla" << std::endl;
    {
        // Como en FL: "SmartKeys Chords" activado en MIDI Settings y SmartKeys seleccionado,
        // así que todo lo que sale por el teclado vuelve a SmartKeys (loopback) y FL lo graba ahí.
        auto setPianoRoll = [&host] (bool on)
        {
            if (auto xml = host.getPluginStateXml())
            {
                xml->setAttribute ("pianoRollChords", on ? 1 : 0);
                host.setPluginStateXml (*xml);
            }
        };
        setPianoRoll (true);
        {
            auto state = host.getPluginStateXml();
            check (state != nullptr && state->getIntAttribute ("pianoRollChords", 0) == 1 && state->getIntAttribute ("chordKeyboard", 0) == 1,
                   "el modo se guarda con el proyecto (y deja el teclado encendido)");
        }
        host.setChoice ("Mic Gate", 0);
        host.setBool ("Mic Voice Controller", true);
        runPortScenario (host, *tap, 0.4, {}, true);

        // a) Se toca en SmartKeys (aquí, con la voz: FL no graba esa entrada)
        const InputTone la3 { 100.0, 700.0, 220.0, 0.3f };
        auto sing = runPortScenario (host, *tap, 1.3, {}, true, 1, nullptr, {}, la3);
        const auto singOut = countNoteOns (sing.out.midi), singPort = countPortNoteOns (sing.port);
        std::cout << "     voz La3: salida MIDI " << describeCounts (singOut) << "   teclado " << describeCounts (singPort)
                  << "   ecos devueltos: " << sing.echoes << std::endl;
        check (singPort == std::map<int, int> { { 57, 1 }, { 60, 1 }, { 64, 1 } } && hangingPortNotes (sing.port) == 0,
               "el acorde completo (La menor) sale una vez por el teclado: es lo que FL graba en el Piano Roll de SmartKeys");
        check (sing.echoes >= 6 && singOut == std::map<int, int> { { 57, 1 }, { 60, 1 }, { 64, 1 } } && hangingNotes (sing.out.midi) == 0,
               "el acorde vuelve de FL (eco) y no suena otra vez: el instrumento y la salida MIDI lo tocan una sola vez");

        // b) Al reproducir, el Piano Roll de SmartKeys le manda el acorde grabado
        auto play = runPortScenario (host, *tap, 0.8, { { 100.0, MidiMessage::noteOn (1, 57, (uint8) 95) },
                                                        { 100.0, MidiMessage::noteOn (1, 60, (uint8) 80) },
                                                        { 100.0, MidiMessage::noteOn (1, 64, (uint8) 90) },
                                                        { 500.0, MidiMessage::noteOff (1, 57) },
                                                        { 500.0, MidiMessage::noteOff (1, 60) },
                                                        { 500.0, MidiMessage::noteOff (1, 64) } }, true);
        std::cout << "     Piano Roll con La menor: salida MIDI " << describeCounts (countNoteOns (play.out.midi))
                  << "   teclado " << describeCounts (countPortNoteOns (play.port)) << std::endl;
        check (countNoteOns (play.out.midi) == std::map<int, int> { { 57, 1 }, { 60, 1 }, { 64, 1 } } && hangingNotes (play.out.midi) == 0,
               "al reproducir, las notas del Piano Roll suenan tal cual (no se convierte cada una en otro acorde)");
        check (play.port.empty(), "y no salen otra vez por el teclado: nada se graba doble");

        // c) Una tecla que llega del DAW (el teclado de FL, que FL ya graba): suena tal cual
        auto key = runPortScenario (host, *tap, 0.6, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                       { 400.0, MidiMessage::noteOff (1, 54) } }, true);
        check (countNoteOns (key.out.midi) == std::map<int, int> { { 54, 1 } } && key.port.empty() && hangingNotes (key.out.midi) == 0,
               "una tecla que llega del DAW suena tal cual, sin acorde, y no sale por el teclado");

        // c2) La misma nota otra vez sin su NoteOff: no se vuelve a atacar
        auto twice = runPortScenario (host, *tap, 0.6, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                         { 220.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                         { 400.0, MidiMessage::noteOff (1, 54) } }, true);
        check (countNoteOns (twice.out.midi) == std::map<int, int> { { 54, 1 } } && hangingNotes (twice.out.midi) == 0 && twice.port.empty(),
               "un NoteOn repetido del Piano Roll no se vuelve a atacar (una nota, un NoteOff)");

        // d) Grabando en bucle: vuelta 1 se canta; vuelta 2, FL repite lo que grabó
        host.transport.bpm = 120.0;
        host.transport.loopBeats = 4.0;
        host.transport.ppq = 0.0;
        host.transport.playing = host.transport.recording = true;
        auto loop = runPortScenario (host, *tap, 4.3, { { 2150.0, MidiMessage::noteOn (1, 57, (uint8) 95) },
                                                        { 2150.0, MidiMessage::noteOn (1, 60, (uint8) 80) },
                                                        { 2150.0, MidiMessage::noteOn (1, 64, (uint8) 90) },
                                                        { 2700.0, MidiMessage::noteOff (1, 57) },
                                                        { 2700.0, MidiMessage::noteOff (1, 60) },
                                                        { 2700.0, MidiMessage::noteOff (1, 64) } }, true, 1, nullptr, {}, la3);
        host.transport.playing = host.transport.recording = false;
        host.transport.loopBeats = 0.0;
        host.transport.ppq = 0.0;
        std::cout << "     grabando en bucle: salida MIDI " << describeCounts (countNoteOns (loop.out.midi))
                  << "   teclado " << describeCounts (countPortNoteOns (loop.port)) << std::endl;
        check (countPortNoteOns (loop.port) == std::map<int, int> { { 57, 1 }, { 60, 1 }, { 64, 1 } }
                 && countNoteOns (loop.out.midi) == std::map<int, int> { { 57, 2 }, { 60, 2 }, { 64, 2 } } && hangingNotes (loop.out.midi) == 0,
               "grabando en bucle, las dos vueltas suenan y el acorde sale una sola vez por el teclado: queda grabado una vez");

        // e) Modo apagado: las teclas del DAW vuelven a ser acordes
        host.setBool ("Mic Voice Controller", false);
        setPianoRoll (false);
        auto normal = runPortScenario (host, *tap, 0.6, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                          { 400.0, MidiMessage::noteOff (1, 54) } }, false);
        check (countNoteOns (normal.out.midi) == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } }
                 && countPortNoteOns (normal.port) == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } },
               "con el modo apagado, una tecla del DAW vuelve a ser el acorde completo (también por el teclado)");

        // f) Una tecla repetida sin soltar: un acorde también por el teclado (lo que FL graba)
        auto repeated = runPortScenario (host, *tap, 0.6, { { 100.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                            { 230.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                            { 400.0, MidiMessage::noteOff (1, 54) } }, false);
        check (countNoteOns (repeated.out.midi) == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } }
                 && countPortNoteOns (repeated.port) == std::map<int, int> { { 53, 1 }, { 57, 1 }, { 60, 1 } }
                 && hangingNotes (repeated.out.midi) == 0 && hangingPortNotes (repeated.port) == 0,
               "un NoteOn repetido sin soltar: un solo acorde por la salida MIDI y por el teclado (nada doble en el Piano Roll)");
        runPortScenario (host, *tap, 0.3, {}, false);
    }

    std::cout << "\n-- P7. Bypass, exportar y cerrar el plugin con un acorde sonando" << std::endl;
    {
        if (auto* bypass = host.instance().getBypassParameter())
        {
            auto held = runPortScenario (host, *tap, 0.3, { { 50.0, MidiMessage::noteOn (1, 54, (uint8) 100) } }, false);
            bypass->setValueNotifyingHost (1.0f);
            auto during = runPortScenario (host, *tap, 0.4, {}, false);
            std::set<int> offs;
            for (auto& e : during.port)
                if (e.isNoteOff())
                    offs.insert (e.data1);
            check (offs == std::set<int> { 53, 57, 60 } && during.out.notesOff (1) == std::set<int> { 53, 57, 60 },
                   "bypass: el acorde que sonaba recibe sus NoteOff (salida MIDI y teclado)");
            bypass->setValueNotifyingHost (0.0f);
            auto after = runPortScenario (host, *tap, 0.5, { { 50.0, MidiMessage::noteOff (1, 54) },
                                                             { 100.0, MidiMessage::noteOn (1, 57, (uint8) 100) },
                                                             { 300.0, MidiMessage::noteOff (1, 57) } }, false);
            check (countPortNoteOns (after.port) == std::map<int, int> { { 57, 1 }, { 60, 1 }, { 64, 1 } },
                   "al quitar el bypass vuelve a funcionar (A3 -> La menor)");
        }
        else
        {
            check (false, "el VST3 tiene parámetro de bypass");
        }

        host.instance().setNonRealtime (true);
        auto offline = runPortScenario (host, *tap, 0.4, { { 50.0, MidiMessage::noteOn (1, 54, (uint8) 100) },
                                                           { 250.0, MidiMessage::noteOff (1, 54) } }, false);
        check (offline.out.notesOn (1) == std::set<int> { 53, 57, 60 } && offline.port.empty(),
               "al exportar (fuera de tiempo real) los acordes salen por MIDI, pero no por el teclado");
        host.instance().setNonRealtime (false);
        runPortScenario (host, *tap, 0.2, {}, false);

        // Apagarlo con un acorde sonando: sus NoteOff salen y el teclado desaparece
        runPortScenario (host, *tap, 0.3, { { 50.0, MidiMessage::noteOn (1, 54, (uint8) 100) } }, false);
        setChordKeyboard (false);
        auto offRun = runPortScenario (host, *tap, 0.6, { { 300.0, MidiMessage::noteOff (1, 54) } }, false);
        std::set<int> offsOff;
        for (auto& e : offRun.port)
            if (e.isNoteOff())
                offsOff.insert (e.data1);
        check (offsOff == std::set<int> { 53, 57, 60 } && countPortNoteOns (offRun.port).empty(),
               "al apagarlo con un acorde sonando, salen sus NoteOff y nada más");
       #if JUCE_MAC
        check (! tap->deviceListed (deviceName), "y el teclado desaparece del sistema");
       #endif
        auto silent = runPortScenario (host, *tap, 0.4, { { 50.0, MidiMessage::noteOn (1, 57, (uint8) 100) },
                                                          { 200.0, MidiMessage::noteOff (1, 57) } }, false);
        check (silent.port.empty() && silent.out.notesOn (1) == std::set<int> { 57, 60, 64 },
               "apagado: los acordes siguen saliendo por la salida MIDI, pero no por el teclado");

        // Encendido otra vez y se cierra el plugin con un acorde pulsado
        setChordKeyboard (true);
        tap = std::make_unique<PortTap> (tapFile.getFullPathName());
        bool reconnected = false;
        for (int i = 0; i < 60 && ! reconnected; ++i)
        {
            runPortScenario (host, *tap, 0.1, {}, false);
            reconnected = tap->connect (deviceName);
        }
        tap->poll();
        check (reconnected, "se vuelve a encender");

        runPortScenario (host, *tap, 0.3, { { 50.0, MidiMessage::noteOn (1, 54, (uint8) 100) } }, false);
        host.unload();
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        std::set<int> offs;
        for (auto& e : tap->poll())
            if (e.isNoteOff())
                offs.insert (e.data1);
        check (offs == std::set<int> { 53, 57, 60 }, "al cerrar el plugin con el acorde pulsado, sus NoteOff salen por el teclado");
       #if JUCE_MAC
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (! tap->deviceListed (deviceName), "y el teclado desaparece del sistema");
       #endif
    }

    tap.reset();
   #if JUCE_LINUX
    unsetenv ("SMARTKEYS_TEST_CHORD_PORT");
    tapFile.deleteFile();
   #endif
   #endif
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

   #if JUCE_MAC
    // Como un DAW: macOS no "duerme" el proceso de pruebas (ver MacActivity.h)
    struct NoAppNap
    {
        void* token = smartkeysBeginLatencyCriticalActivity();
        ~NoAppNap() { smartkeysEndActivity (token); }
    } noAppNap;
   #endif

    if (argc < 2)
    {
        std::cout << "Uso: SmartKeysHostTests <SmartKeys.vst3> --synth <SmartKeysTestSynth.vst3> [--synth-au] "
                     "[--stress <segundos>] [--system-mic-only] [--port-only] [--host-only]" << std::endl;
        return 2;
    }

    // Rutas absolutas (juce::File no admite rutas relativas)
    const auto cwd = File::getCurrentWorkingDirectory();
    double stressSeconds = 4.0;
    bool systemMicOnly = false, portOnly = false, hostOnly = false, instrumentOnly = false;
    StringArray paths;
    for (int i = 1; i < argc; ++i)
    {
        const auto arg = String::fromUTF8 (argv[i]);
        if (arg == "--stress" && i + 1 < argc)
            stressSeconds = String::fromUTF8 (argv[++i]).getDoubleValue();
        else if (arg == "--synth" && i + 1 < argc)
            gSynthPath = cwd.getChildFile (String::fromUTF8 (argv[++i])).getFullPathName();
        else if (arg == "--synth-au")
            gSynthAU = true;
        else if (arg == "--system-mic-only")
            systemMicOnly = true;
        else if (arg == "--port-only")
            portOnly = true;
        else if (arg == "--host-only")
            hostOnly = true;
        else if (arg == "--instrument-only")
            instrumentOnly = true;
        else
            paths.add (cwd.getChildFile (arg).getFullPathName());
    }

    // Lo que apunta el instrumento de prueba, su latencia, y la carpeta de datos de
    // SmartKeys (catálogo, presets, carga protegida) aparte de la del usuario
    const auto workDir = File::createTempFile ("smartkeys-hosttests");
    workDir.createDirectory();
    gSynthLog = workDir.getChildFile ("testsynth.log");
   #if JUCE_WINDOWS
    _putenv_s ("SMARTKEYS_TESTSYNTH_LOG", gSynthLog.getFullPathName().toRawUTF8());
    _putenv_s ("SMARTKEYS_TESTSYNTH_LATENCY", String (kSynthLatency).toRawUTF8());
    _putenv_s ("SMARTKEYS_USER_DATA", workDir.getChildFile ("userdata").getFullPathName().toRawUTF8());
   #else
    setenv ("SMARTKEYS_TESTSYNTH_LOG", gSynthLog.getFullPathName().toRawUTF8(), 1);
    setenv ("SMARTKEYS_TESTSYNTH_LATENCY", String (kSynthLatency).toRawUTF8(), 1);
    setenv ("SMARTKEYS_USER_DATA", workDir.getChildFile ("userdata").getFullPathName().toRawUTF8(), 1);
   #endif

    if (instrumentOnly)
    {
        testInstrument (paths[0]);
        std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
        workDir.deleteRecursively();
        return numFailures == 0 ? 0 : 1;
    }

    if (hostOnly)
    {
        testHostedInstrument (paths[0]);
        testCrashProtection (paths[0]);
        if (stressSeconds > 0.0)
            testConcurrency (paths[0], stressSeconds);
        std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
        workDir.deleteRecursively();
        return numFailures == 0 ? 0 : 1;
    }

    if (! systemMicOnly && ! portOnly)
    {
        testInstrument (paths[0]);
        testHostedInstrument (paths[0]);
        testCrashProtection (paths[0]);
    }

    if (! portOnly)
        testSystemMic (paths[0]);

    if (! systemMicOnly)
        testChordPort (paths[0]);

    if (! systemMicOnly && ! portOnly && stressSeconds > 0.0)
    {
       #if JUCE_LINUX
        setenv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3", 1);   // el micrófono también se abre y cierra
        setenv ("SMARTKEYS_TEST_CHORD_PORT", "1", 1);             // y el teclado virtual (sin archivo)
       #endif
       #if ! JUCE_WINDOWS
        setenv ("SMARTKEYS_CHORD_PORT", "1", 1);                  // encendido de entrada
       #endif
        testConcurrency (paths[0], stressSeconds);
       #if ! JUCE_WINDOWS
        unsetenv ("SMARTKEYS_CHORD_PORT");
       #endif
       #if JUCE_LINUX
        unsetenv ("SMARTKEYS_FAKE_MIC");
        unsetenv ("SMARTKEYS_TEST_CHORD_PORT");
       #endif
    }

    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    workDir.deleteRecursively();
    return numFailures == 0 ? 0 : 1;
}
