/*
  ==============================================================================

    HostTests.cpp
    SmartKeys — prueba de integración: carga los VST3 COMPILADOS como lo haría
    un DAW y comprueba, bloque a bloque, lo que sale del plugin:

      1. Cada tecla sale por MIDI OUT como el ACORDE COMPLETO (lo que pinta el
         Piano Roll del instrumento destino), en el canal MIDI elegido, y los
         NoteOff llegan por el mismo canal aunque se cambie con la tecla pulsada.
      2. OUTPUT MODE: INTERNAL PIANO (audio, sin MIDI) · EXTERNAL PLUGIN (MIDI,
         audio en silencio) · BOTH (audio + MIDI).
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

        plugin->processBlock (buffer, midi);

        BlockResult r;
        for (const auto meta : midi)
            r.midi.push_back (meta.getMessage());
        for (int ch = 0; ch < numOuts; ++ch)
            r.outputRms = jmax (r.outputRms, buffer.getRMSLevel (ch, 0, blockSize));
        return r;
    }

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
    host.setChoice ("Output Mode", 2);
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

    std::cout << "\n-- 3. OUTPUT MODE" << std::endl;
    {
        host.setChoice ("Output Mode", 1);   // EXTERNAL PLUGIN (MIDI OUT)
        host.run();
        auto r = host.run ({ MidiMessage::noteOn (1, 64, (uint8) 100) });   // E4 -> Em
        auto tail = host.runBlocks (4);
        check (r.notesOn (1) == std::set<int> { 64, 67, 71 }, "EXTERNAL: el acorde Em sale por MIDI");
        check (jmax (r.outputRms, tail.outputRms) < 1.0e-9f, "EXTERNAL: el piano interno queda en silencio absoluto");
        host.run ({ MidiMessage::noteOff (1, 64) });

        host.setChoice ("Output Mode", 0);   // INTERNAL PIANO
        host.run();
        auto r2 = host.run ({ MidiMessage::noteOn (1, 65, (uint8) 100) });   // F4
        auto tail2 = host.runBlocks (4);
        check (r2.midi.empty() && tail2.midi.empty(), "INTERNAL PIANO: no sale MIDI");
        check (jmax (r2.outputRms, tail2.outputRms) > 1.0e-3f, "INTERNAL PIANO: suena el piano interno");
        host.run ({ MidiMessage::noteOff (1, 65) });

        host.setChoice ("Output Mode", 2);   // BOTH
        host.run();
        auto r3 = host.run ({ MidiMessage::noteOn (1, 69, (uint8) 100) });   // A4 -> Am
        auto tail3 = host.runBlocks (4);
        check (r3.notesOn (1) == std::set<int> { 69, 72, 76 } && jmax (r3.outputRms, tail3.outputRms) > 1.0e-3f,
               "BOTH: acorde Am por MIDI y piano interno a la vez");
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
        host.setChoice ("Output Mode", 2);
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
    host.setChoice ("Output Mode", 2);
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
