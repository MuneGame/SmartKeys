/*
  ==============================================================================

    HostTests.cpp
    SmartKeys — prueba de integración: carga los VST3 COMPILADOS como lo haría
    un DAW y comprueba, bloque a bloque, lo que sale del plugin:

      1. Cada tecla sale por MIDI OUT como el ACORDE COMPLETO (lo que pinta el
         Piano Roll del instrumento destino), en el canal MIDI elegido, y los
         NoteOff llegan por el mismo canal aunque se cambie con la tecla pulsada.
      2. OUTPUT MODE: la salida MIDI está SIEMPRE activa; PIANO + MIDI (audio y
         MIDI) · MIDI OUT (MIDI, piano interno en silencio absoluto).
      2b. STRUM: las notas del acorde entran escalonadas ASC/DESC, exactas a la
         muestra, y un NoteOff anticipado no deja notas colgadas.
      2c. DRAG CHORDS: con el DAW detenido no se guarda nada; con el DAW en PLAY
         cada nota suelta del Piano Roll se guarda como acorde completo en su
         tiempo exacto; en bucle sólo queda la última vuelta.
      3. COMPLEX CHORDS: 5–7 notas por tecla y el mismo número de NoteOff.
      4. Voz por la entrada del DAW (vía A): una voz cantando La3 dispara el
         acorde La menor, se suelta al callar, y la voz cruda NUNCA sale.
      5. SmartKeys FX: la voz de la pista (entrada principal) dispara acordes y
         se reemplaza por el piano.

    Uso:  SmartKeysHostTests <SmartKeys.vst3> [<SmartKeys FX.vst3>]

  ==============================================================================
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <set>
#include <map>
#include <vector>

using namespace juce;

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

struct TimedNote { double beatOn, beatOff; int note; };
struct SavedChord { double start = 0.0, length = 0.0; std::set<int> notes; };

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
    bool isInstrument() const { return plugin != nullptr && plugin->getPluginDescription().isInstrument; }
    int getNumInputs() const  { return numIns; }

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

    BlockResult process()
    {
        plugin->processBlock (buffer, midi);

        BlockResult r;
        for (const auto meta : midi)
        {
            auto m = meta.getMessage();
            m.setTimeStamp ((double) (sampleClock + meta.samplePosition));
            r.midi.push_back (m);
        }
        for (int ch = 0; ch < numOuts; ++ch)
            r.outputRms = jmax (r.outputRms, buffer.getRMSLevel (ch, 0, blockSize));

        sampleClock += blockSize;
        if (transport.playing || transport.recording)
            transport.ppq += (double) blockSize * transport.bpm / (60.0 * sampleRate);
        return r;
    }

    // Reproduce 'lengthBeats' desde 'startBeat' con el DAW en PLAY tocando 'notes'
    // (como un Piano Roll con notas sueltas) y devuelve todo el MIDI de salida
    BlockResult playPattern (double startBeat, double lengthBeats, const std::vector<TimedNote>& notes, bool keepPlaying = false)
    {
        transport.playing = true;
        transport.ppq = startBeat;
        const double samplesPerBeat = 60.0 * sampleRate / transport.bpm;
        const auto numBlocks = (int) std::ceil (lengthBeats * samplesPerBeat / blockSize);

        BlockResult total;
        for (int b = 0; b < numBlocks; ++b)
        {
            const double blockStartBeat = transport.ppq;
            std::vector<std::pair<int, MidiMessage>> events;
            for (const auto& n : notes)
            {
                // Muestra más cercana al tiempo de la nota (como un Piano Roll real)
                const auto on  = (int) std::llround ((n.beatOn  - blockStartBeat) * samplesPerBeat);
                const auto off = (int) std::llround ((n.beatOff - blockStartBeat) * samplesPerBeat);
                if (on >= 0 && on < blockSize)
                    events.push_back ({ on, MidiMessage::noteOn (1, n.note, (uint8) 100) });
                if (off >= 0 && off < blockSize)
                    events.push_back ({ off, MidiMessage::noteOff (1, n.note) });
            }
            std::sort (events.begin(), events.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
            auto r = runAt (events);
            total.midi.insert (total.midi.end(), r.midi.begin(), r.midi.end());
            total.outputRms = jmax (total.outputRms, r.outputRms);
        }

        if (! keepPlaying)
        {
            transport.playing = false;
            runBlocks (2);   // el plugin ve el STOP
        }
        return total;
    }

    // Progresión guardada en el estado del plugin (lo mismo que exporta DRAG CHORDS)
    std::vector<SavedChord> getProgression()
    {
        // En Linux el VST3 tiene su propio hilo de mensajes (sin editor abierto):
        // su temporizador (25 Hz) vacía la cola de captura; basta con esperar.
        Thread::sleep (200);

        MemoryBlock state;
        plugin->getStateInformation (state);
        std::vector<SavedChord> chords;
        auto xml = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());

        // El host VST3 envuelve el estado del plugin en <VST3PluginState><IComponent>base64</IComponent>
        if (xml != nullptr && xml->hasTagName ("VST3PluginState"))
        {
            std::unique_ptr<XmlElement> inner;
            if (auto* component = xml->getChildByName ("IComponent"))
            {
                MemoryBlock data;
                if (data.fromBase64Encoding (component->getAllSubText().trim()))
                    inner = AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
            }
            xml = std::move (inner);
        }

        if (xml != nullptr && SystemStats::getEnvironmentVariable ("SK_DEBUG_STATE", {}).isNotEmpty())
            std::cout << xml->toString().toStdString() << std::endl;
        if (xml != nullptr)
            if (auto* p = xml->getChildByName ("Progression"))
                for (auto* c : p->getChildWithTagNameIterator ("Chord"))
                {
                    SavedChord sc;
                    sc.start = c->getDoubleAttribute ("start");
                    sc.length = c->getDoubleAttribute ("length");
                    StringArray tokens;
                    tokens.addTokens (c->getStringAttribute ("notes"), " ", "");
                    for (auto& t : tokens)
                        if (t.isNotEmpty())
                            sc.notes.insert (t.getIntValue());
                    chords.push_back (sc);
                }
        return chords;
    }

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

    // Do mayor, SMART CHORDS, tríadas, OUTPUT = BOTH, canal 1
    host.setChoice ("Key", 0);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Chord Type", 0);
    host.setChoice ("Output Mode", 0);
    host.setChoice ("MIDI Out Channel", 0);
    host.setBool ("Mic Voice Controller", false);
    host.settle (4);

    std::cout << "\n-- 1. Piano Roll: una tecla -> acorde completo por MIDI OUT" << std::endl;
    {
        auto r = host.run ({ MidiMessage::noteOn (1, 62, (uint8) 100) });   // D4
        std::cout << "     D4 -> " << describe (r.notesOn()) << std::endl;
        check (r.notesOn (1) == std::set<int> { 62, 65, 69 }, "D4 en Do mayor sale como Dm completo {D4 F4 A4} por el canal 1");
        auto tail = host.runBlocks (3);
        check (tail.outputRms > 1.0e-3f || r.outputRms > 1.0e-3f, "BOTH: el piano interno suena");
        auto off = host.run ({ MidiMessage::noteOff (1, 62) });
        check (off.notesOff (1) == std::set<int> { 62, 65, 69 }, "al soltar la tecla salen los 3 NoteOff");
    }

    std::cout << "\n-- 2. Canal MIDI de salida" << std::endl;
    {
        host.setChoice ("MIDI Out Channel", 4);   // canal 5
        auto r = host.run ({ MidiMessage::noteOn (1, 67, (uint8) 90) });   // G4 -> G mayor
        std::cout << "     G4 -> " << describe (r.notesOn()) << " canales " << r.channelsUsed().size() << std::endl;
        check (r.notesOn (5) == std::set<int> { 67, 71, 74 } && r.channelsUsed() == std::set<int> { 5 },
               "con MIDI CH 5 el acorde G sale sólo por el canal 5");
        host.setChoice ("MIDI Out Channel", 8);   // se cambia con la tecla pulsada
        host.run();
        auto off = host.run ({ MidiMessage::noteOff (1, 67) });
        check (off.notesOff (5) == std::set<int> { 67, 71, 74 }, "los NoteOff salen por el canal 5 aunque se cambie a 9 con la tecla pulsada");
        host.setChoice ("MIDI Out Channel", 0);
        host.settle();
    }

    std::cout << "\n-- 3. OUTPUT MODE (la salida MIDI está siempre activa)" << std::endl;
    {
        host.setChoice ("Output Mode", 1);   // MIDI OUT (piano interno en silencio)
        host.run();
        auto r = host.run ({ MidiMessage::noteOn (1, 64, (uint8) 100) });   // E4 -> Em
        auto tail = host.runBlocks (4);
        check (r.notesOn (1) == std::set<int> { 64, 67, 71 }, "MIDI OUT: el acorde Em sale por MIDI");
        check (jmax (r.outputRms, tail.outputRms) < 1.0e-9f, "MIDI OUT: el piano interno queda en silencio absoluto");
        host.run ({ MidiMessage::noteOff (1, 64) });

        host.setChoice ("Output Mode", 0);   // PIANO + MIDI (por defecto)
        host.run();
        auto r2 = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });   // A4 -> Am
        auto tail2 = host.runBlocks (4);
        check (r2.notesOn (1) == std::set<int> { 69, 72, 76 }, "PIANO + MIDI: el acorde Am completo sale por MIDI");
        check (jmax (r2.outputRms, tail2.outputRms) > 1.0e-3f, "PIANO + MIDI: y suena el piano interno a la vez");
        host.run ({ MidiMessage::noteOff (1, 69) });
        host.settle();
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
        if (auto* p = host.findParameter ("Strum"))
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

        if (auto* p = host.findParameter ("Strum"))
            p->setValueNotifyingHost (0.0f);
        host.setChoice ("Strum Direction", 0);
        host.settle();
    }

    std::cout << "\n-- 4c. DRAG CHORDS: sólo se graba con el DAW en PLAY / REC" << std::endl;
    {
        host.setChoice ("Mode", 0);
        host.transport.playing = false;
        host.run ({ MidiMessage::noteOn (1, 60, (uint8) 100) });
        host.runBlocks (10);
        host.run ({ MidiMessage::noteOff (1, 60) });
        check (host.getProgression().empty(), "DAW detenido: probar teclas NO guarda nada");

        // Piano Roll con notas sueltas F4, C5, B4, A4 (1 tiempo cada una) -> PLAY
        const std::vector<TimedNote> pianoRoll { { 0.0, 0.95, 65 }, { 1.0, 1.95, 72 }, { 2.0, 2.95, 71 }, { 3.0, 3.95, 69 } };
        host.playPattern (0.0, 4.0, pianoRoll);
        const auto chords = host.getProgression();
        std::cout << "     capturado:";
        for (auto& c : chords)
        {
            std::cout << " [" << c.start << " +" << String (c.length, 2) << ":";
            for (int n : c.notes) std::cout << " " << n;
            std::cout << "]";
        }
        std::cout << std::endl;
        const bool okCount = chords.size() == 4;
        check (okCount, "4 notas sueltas del Piano Roll -> 4 acordes en la progresión");
        if (okCount)
        {
            check (chords[0].notes == std::set<int> { 65, 69, 72 } && chords[1].notes == std::set<int> { 72, 76, 79 }
                   && chords[2].notes == std::set<int> { 71, 74, 77 } && chords[3].notes == std::set<int> { 69, 72, 76 },
                   "F Maj, C Maj, B dim, A min: todas las notas de cada acorde apiladas");
            bool timing = true;
            for (int i = 0; i < 4; ++i)
                timing = timing && std::abs (chords[(size_t) i].start - (double) i) < 0.002
                                && std::abs (chords[(size_t) i].length - 0.95) < 0.02;
            check (timing, "cada acorde en su tiempo exacto del compás y con la duración de la nota original");
        }

        // Bucle: la segunda vuelta reemplaza a la primera (sin duplicar acordes)
        host.playPattern (0.0, 4.0, { { 0.0, 0.9, 60 }, { 2.0, 2.9, 67 } }, true);
        host.playPattern (0.0, 4.0, { { 1.0, 1.9, 65 } });
        const auto looped = host.getProgression();
        check (looped.size() == 1 && looped[0].notes == std::set<int> { 65, 69, 72 } && std::abs (looped[0].start - 1.0) < 0.002,
               "patrón en bucle: sólo queda la última vuelta");

        // DAW en PLAY sin notas: la progresión no se toca
        host.playPattern (0.0, 4.0, {});
        check (host.getProgression().size() == 1, "reproducir sin tocar nada no borra la progresión");
    }

    std::cout << "\n-- 5. La voz cruda nunca sale por el plugin" << std::endl;
    {
        host.setBool ("Mic Voice Controller", false);
        auto r = host.runBlocks (20, 220.0, 0.5f);
        check (r.outputRms < 1.0e-4f, "con voz fuerte en la entrada y sin notas, la salida está en silencio (buffer.clear())");
    }

    std::cout << "\n-- 6. Voz -> acorde por la entrada del DAW (vía A)" << std::endl;
    {
        host.setChoice ("Mic Source", 0);   // DAW INPUT
        host.setChoice ("Mic Gate", 0);     // umbral mínimo: no influye en la prueba
        host.setBool ("Mic Voice Controller", true);
        host.setChoice ("Output Mode", 1);  // sólo MIDI, para medir el silencio del audio
        host.run();

        auto sing = host.runBlocks (40, 220.0, 0.3f);   // La3 cantado ~0.4 s
        const auto on = sing.notesOn (1);
        std::cout << "     voz La3 (220 Hz) -> " << describe (on) << std::endl;
        check (on.size() == 3 && pitchClasses (on) == std::set<int> { 9, 0, 4 }, "cantar La3 dispara el acorde La menor completo por MIDI");
        check (sing.outputRms < 1.0e-9f, "la voz no se cuela en la salida de audio");

        auto quiet = host.runBlocks (30);
        check (! on.empty() && quiet.notesOff (1) == on, "al dejar de cantar se sueltan todas las notas del acorde");
        host.setBool ("Mic Voice Controller", false);
        host.setChoice ("Output Mode", 0);
    }

    host.unload();
}

//==============================================================================
void testFx (const String& path)
{
    std::cout << "\n=== SmartKeys FX (efecto para la pista del micrófono) ===" << std::endl;
    Host host;
    if (! host.load (path)) { check (false, "cargar el plugin"); return; }

    check (! host.isInstrument(), "se registra como efecto (se inserta en la pista del micrófono del Mixer)");
    check (host.getNumInputs() >= 1, "recibe el audio de la pista como entrada principal");

    host.setChoice ("Key", 0);
    host.setChoice ("Scale", 0);
    host.setChoice ("Mode", 0);
    host.setChoice ("Output Mode", 0);
    host.setChoice ("MIDI Out Channel", 0);
    host.setChoice ("Mic Source", 0);
    host.setChoice ("Mic Gate", 0);
    host.setBool ("Mic Voice Controller", false);
    host.settle (4);

    auto silent = host.runBlocks (20, 220.0, 0.5f);
    check (silent.outputRms < 1.0e-4f, "MIC VOICE apagado: la voz de la pista no pasa (sale silencio)");

    host.setBool ("Mic Voice Controller", true);
    host.run();
    auto sing = host.runBlocks (40, 220.0, 0.3f);
    const auto on = sing.notesOn (1);
    std::cout << "     voz La3 (220 Hz) -> " << describe (on) << "  RMS salida " << sing.outputRms << std::endl;
    check (on.size() == 3 && pitchClasses (on) == std::set<int> { 9, 0, 4 }, "la voz de la pista dispara La menor por MIDI OUT");
    check (sing.outputRms > 1.0e-3f, "y la pista suena con el piano de SmartKeys en lugar de la voz");

    auto quiet = host.runBlocks (30);
    check (! on.empty() && quiet.notesOff (1) == on, "al callar se sueltan las notas");

    host.setBool ("Mic Voice Controller", false);
    auto key = host.run ({ MidiMessage::noteOn (1, 60, (uint8) 100) });
    check (key.notesOn (1) == std::set<int> { 60, 64, 67 }, "también acepta notas MIDI: C4 -> C mayor completo");
    host.run ({ MidiMessage::noteOff (1, 60) });

    host.unload();
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "Uso: SmartKeysHostTests <SmartKeys.vst3> [<SmartKeys FX.vst3>]" << std::endl;
        return 2;
    }

    // Rutas absolutas (juce::File no admite rutas relativas)
    const auto cwd = File::getCurrentWorkingDirectory();
    testInstrument (cwd.getChildFile (String::fromUTF8 (argv[1])).getFullPathName());
    if (argc > 2)
        testFx (cwd.getChildFile (String::fromUTF8 (argv[2])).getFullPathName());

    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    return numFailures == 0 ? 0 : 1;
}
