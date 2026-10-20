/*
  SmartKeys — Pruebas del núcleo DSP (sin GUI ni host).
  Compilar: cmake -B build -DSMARTKEYS_BUILD_TESTS=ON && cmake --build build --target SmartKeysTests
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#include "ScaleChordEngine.h"
#include "VoicePitchTracker.h"
#include "FlKeysPianoSynth.h"
#include "MidiOutputRouter.h"
#include "StrumScheduler.h"
#include "MicRingBuffer.h"
#include "SystemMicInput.h"
#include "ChordMidiPort.h"
#include <thread>
#include <chrono>

#if defined (__GNUC__) && ! defined (__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

//==============================================================================
// Contador global de asignaciones para verificar el camino de audio lock-free
static std::atomic<long> gAllocations { 0 };
static std::atomic<bool> gCountAllocations { false };

void* operator new (std::size_t size)
{
    if (gCountAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)                 { return operator new (size); }
void operator delete (void* p) noexcept                 { std::free (p); }
void operator delete[] (void* p) noexcept               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept  { std::free (p); }

struct AllocationGuard
{
    AllocationGuard()  { gAllocations = 0; gCountAllocations = true; }
    ~AllocationGuard() { gCountAllocations = false; }
    long count() const { return gAllocations.load(); }
};

//==============================================================================
static int gFailures = 0, gChecks = 0;
#define CHECK(cond, msg) do { ++gChecks; if (! (cond)) { ++gFailures; std::printf ("  [FALLO] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

using namespace smartkeys;

static std::vector<int> chordOf (ScaleChordEngine& e, int note)
{
    std::vector<int> v; v.reserve (16);
    e.generateOutputNotes (note, v);
    return v;
}

static juce::String vecToString (const std::vector<int>& v)
{
    juce::String s;
    for (int n : v) s << n << " ";
    return s.trim();
}

//==============================================================================
static void testScaleDatabase()
{
    std::printf ("[1] Base de datos de escalas\n");
    CHECK (ScaleChordEngine::getNumScales() == 43, "43 escalas");

    int counts[4] = {};
    for (int i = 0; i < kNumScales; ++i)
    {
        const auto& s = ScaleChordEngine::getScale (i);
        counts[(int) s.category]++;
        CHECK (s.numDegrees >= 5 && s.numDegrees <= 12, "grados válidos");
        CHECK (s.degrees[0].semitone == 0, "primer grado = tónica");
        for (int d = 1; d < s.numDegrees; ++d)
            CHECK (s.degrees[d].semitone > s.degrees[d - 1].semitone, "grados ascendentes y únicos");
    }
    CHECK (counts[0] == 3 && counts[1] == 21 && counts[2] == 7 && counts[3] == 12, "3 básicas / 21 occidentales / 7 maqam / 12 raga");
    CHECK (ScaleChordEngine::getPitchClassMask (5, 0) == 0b011010110101, "máscara de Fa Mayor");
}

static void testQuantization()
{
    std::printf ("[2] Cuantización SINGLE NOTE\n");
    ScaleChordEngine e;
    e.setState (0, 0, ScaleChordEngine::getPitchClassMask (0, 0));   // C Major
    e.setMode (OperationMode::SingleNote);

    CHECK (e.quantizeNote (60) == 60, "C4 -> C4");
    CHECK (e.quantizeNote (61) == 60, "C#4 -> C4 (empate hacia abajo)");
    CHECK (e.quantizeNote (66) == 65, "F#4 -> F4");
    CHECK (e.quantizeNote (70) == 69, "A#4 -> A4");
    CHECK (chordOf (e, 63).size() == 1 && chordOf (e, 63)[0] == 62, "D#4 -> una sola nota D4");
    CHECK (e.quantizeFloatNote (61.6f) == 62, "61.6 -> D4");
    CHECK (e.quantizeFloatNote (60.9f) == 60, "60.9 -> C4");

    // Frecuencia de referencia
    e.setReferencePitch (442.0f);
    CHECK (std::abs (e.midiNoteToFrequency (69.0f) - 442.0f) < 0.001f, "A4 = 442 Hz");
    CHECK (std::abs (e.frequencyToMidiNote (442.0f) - 69.0f) < 0.001f, "442 Hz = MIDI 69");
    e.setReferencePitch (440.0f);
    CHECK (std::abs (e.midiNoteToFrequency (60.0f) - 261.6256f) < 0.01f, "C4 = 261.63 Hz");
}

static void testSmartChords()
{
    std::printf ("[3] SMART CHORD (ejemplo del documento: Fa Mayor)\n");
    ScaleChordEngine e;
    e.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));   // F Major
    e.setMode (OperationMode::SmartChord);
    e.setChordExtension (ChordExtension::Triad);

    auto c = chordOf (e, 60), d = chordOf (e, 62), ee = chordOf (e, 64);
    CHECK (vecToString (c) == "60 64 67", "Do -> C-E-G");
    CHECK (vecToString (d) == "62 65 69", "Re -> D-F-A");
    CHECK (vecToString (ee) == "64 67 70", "Mi -> E-G-Bb");
    CHECK (e.getChordName (c) == "C Maj", "nombre C Maj");
    CHECK (e.getChordName (d) == "Dm", "nombre Dm");
    CHECK (e.getChordName (ee) == "E dim", "nombre E dim");
    CHECK (e.getChordName (chordOf (e, 53)) == "F Maj", "nombre F Maj");
    CHECK (e.getChordName (chordOf (e, 58)) == "Bb Maj", "bemoles en Fa Mayor");
    CHECK (vecToString (chordOf (e, 59)) == vecToString (chordOf (e, 58)), "B natural (fuera) -> se cuantiza a Bb");

    std::printf ("    Fa Mayor: C=%s  D=%s  E=%s\n", e.getChordName (c).toRawUTF8(), e.getChordName (d).toRawUTF8(), e.getChordName (ee).toRawUTF8());

    e.setChordExtension (ChordExtension::Seventh);
    CHECK (e.getChordName (chordOf (e, 67)) == "Gm7", "G en Fa Mayor con 7ª -> Gm7");
    CHECK (e.getChordName (chordOf (e, 60)) == "C7", "C en Fa Mayor con 7ª -> C7");
    CHECK (e.getChordName (chordOf (e, 65)) == "F Maj7", "F con 7ª -> F Maj7");
    CHECK (e.getChordName (chordOf (e, 64)) == "Em7b5", "E con 7ª -> Em7b5");

    e.setChordExtension (ChordExtension::TriadOctave);
    CHECK (vecToString (chordOf (e, 65)) == "65 69 72 77", "F + octava");

    // Escalas no heptatónicas (búsqueda terciana)
    e.setChordExtension (ChordExtension::Triad);
    e.setState (0, 18, ScaleChordEngine::getPitchClassMask (0, 18));   // C Major Pentatonic
    CHECK (e.getChordName (chordOf (e, 60)) == "C Maj", "pentatónica: C -> C Maj");
    CHECK (e.getChordName (chordOf (e, 62)) == "Dsus4", "pentatónica: D -> Dsus4");
    e.setState (0, 2, ScaleChordEngine::getPitchClassMask (0, 2));     // Chromatic
    CHECK (vecToString (chordOf (e, 61)) == "61 65 68", "cromática: C# -> C# Maj");

    // Notas en los extremos del rango
    e.setState (0, 0, ScaleChordEngine::getPitchClassMask (0, 0));
    auto top = chordOf (e, 127);
    for (int n : top) CHECK (n >= 0 && n <= 127, "acorde en rango 0..127");
}

static void testAllScalesExhaustive()
{
    std::printf ("[4] Exhaustivo: 43 escalas x 12 tónicas x 128 notas x 3 extensiones x 2 modos\n");
    ScaleChordEngine e;
    long combos = 0;
    int badChords = 0;
    for (int s = 0; s < kNumScales; ++s)
        for (int root = 0; root < 12; ++root)
        {
            const auto mask = ScaleChordEngine::getPitchClassMask (root, s);
            e.setState (root, s, mask);
            for (int ext = 0; ext < 3; ++ext)
                for (int mode = 0; mode < 2; ++mode)
                {
                    e.setChordExtension ((ChordExtension) ext);
                    e.setMode ((OperationMode) mode);
                    for (int n = 0; n < 128; ++n)
                    {
                        auto v = chordOf (e, n);
                        ++combos;
                        bool ok = ! v.empty() && v.size() <= 4;
                        for (size_t i = 0; i < v.size(); ++i)
                        {
                            ok = ok && v[i] >= 0 && v[i] <= 127 && ((mask >> (v[i] % 12)) & 1u);
                            if (i > 0) ok = ok && v[i] > v[i - 1];
                        }
                        if (mode == 0 && n >= 12 && n <= 100)
                            ok = ok && v.size() >= 3;
                        if (! ok) ++badChords;
                    }
                }
        }
    std::printf ("    %ld combinaciones evaluadas\n", combos);
    CHECK (badChords == 0, "todas las notas generadas pertenecen a la escala, ordenadas y en rango");
}

static void testCustomMask()
{
    std::printf ("[5] Personalización manual de notas\n");
    ScaleChordEngine e;
    e.setState (0, 0, ScaleChordEngine::getPitchClassMask (0, 0));
    e.setNoteActive (6, true);     // añade F#
    CHECK (e.quantizeNote (66) == 66, "F# activado manualmente");
    CHECK (e.isCustomised(), "marca escala personalizada");
    e.setNoteActive (6, false);
    CHECK (! e.isCustomised(), "vuelve a la escala original");
    e.setCustomMask (0);
    CHECK (e.getNumScaleNotes() == 128, "máscara vacía -> se comporta como cromática (sin crash)");
}

static void testMicrotuning()
{
    std::printf ("[6] Afinación microtonal\n");
    ScaleChordEngine e;
    const int rast = 24;
    e.setState (0, rast, ScaleChordEngine::getPitchClassMask (0, rast));
    CHECK (juce::String::fromUTF8 (ScaleChordEngine::getScale (rast).name) == "Maqam Rast (Rast-Rast)", "índice Rast");
    const float e4 = e.getTunedFrequency (64, true), e4tet = e.getTunedFrequency (64, false);
    const float cents = 1200.0f * std::log2 (e4 / e4tet);
    CHECK (std::abs (cents + 50.0f) < 0.01f, "Rast: E medio-bemol = -50 cents");
    std::printf ("    Rast E4: %.2f Hz (12-TET %.2f Hz, %.1f cents)\n", e4, e4tet, cents);
}

//==============================================================================
static void testRouter()
{
    std::printf ("[7] Router MIDI + registro anti-notas colgadas\n");
    ScaleChordEngine e;
    e.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));
    MidiOutputRouter router;
    juce::MidiBuffer out;
    out.ensureSize (8192);

    auto countType = [&] (bool on, std::vector<int>* notes = nullptr)
    {
        int c = 0;
        for (const auto m : out)
        {
            const auto msg = m.getMessage();
            if ((on && msg.isNoteOn()) || (! on && msg.isNoteOff()))
            {
                ++c;
                if (notes) notes->push_back (msg.getNoteNumber());
            }
        }
        return c;
    };

    router.handleNoteOn (MidiOutputRouter::Source::Midi, 62, 1, 100, 0, e, out);
    std::vector<int> ons;
    CHECK (countType (true, &ons) == 3, "1 tecla -> 3 NoteOn en el Piano Roll");
    CHECK (vecToString (ons) == "62 65 69", "notas D-F-A");

    // Cambio de escala y de modo mientras la tecla sigue pulsada
    e.setState (2, 0, ScaleChordEngine::getPitchClassMask (2, 0));
    e.setMode (OperationMode::SingleNote);
    out.clear();
    router.handleNoteOff (MidiOutputRouter::Source::Midi, 62, 10, out);
    std::vector<int> offs;
    CHECK (countType (false, &offs) == 3, "NoteOff libera las 3 notas originales");
    CHECK (vecToString (offs) == "62 65 69", "mismas notas aunque cambió la escala");

    // Notas compartidas entre dos acordes (C Major: C-E-G y A-C-E)
    e.setState (0, 0, ScaleChordEngine::getPitchClassMask (0, 0));
    e.setMode (OperationMode::SmartChord);
    out.clear();
    router.handleNoteOn (MidiOutputRouter::Source::Midi, 60, 1, 100, 0, e, out);
    router.handleNoteOn (MidiOutputRouter::Source::Midi, 57, 1, 100, 5, e, out);   // A3-C4-E4
    out.clear();
    router.handleNoteOff (MidiOutputRouter::Source::Midi, 60, 20, out);
    offs.clear();
    countType (false, &offs);
    CHECK (vecToString (offs) == "67", "soltar C-E-G sólo apaga G (C y E siguen en A-C-E)");
    CHECK (router.isNoteSounding (57) && router.isNoteSounding (60) && router.isNoteSounding (64), "A-C-E sigue sonando");
    out.clear();
    router.handleNoteOff (MidiOutputRouter::Source::Midi, 57, 30, out);
    uint64_t lo, hi;
    router.getActiveBitmask (lo, hi);
    CHECK (lo == 0 && hi == 0, "nada queda colgado");

    // Voz + teclado independientes; pánico
    out.clear();
    router.handleNoteOn (MidiOutputRouter::Source::Voice, 57, 1, 90, 0, e, out);
    router.handleNoteOn (MidiOutputRouter::Source::Midi, 57, 1, 90, 0, e, out);
    router.releaseAll (0, out);
    router.getActiveBitmask (lo, hi);
    CHECK (lo == 0 && hi == 0, "releaseAll limpia todo");

    const auto ev = MidiOutputRouter::decodeLastEvent (router.lastEventPacked.load());
    CHECK (ev.valid && ev.triggerNote == 57 && ev.notes.size() == 3, "empaquetado del último evento");
}

//==============================================================================
static float runTracker (double sr, float freq, float amplitude, float seconds, int& noteOns, int& noteOffs,
                         int& firstNote, int& firstOnSample, float vibratoCents = 0.0f, bool harmonic = false,
                         int scaleIndex = 2, int root = 0, float* detectedHz = nullptr)
{
    ScaleChordEngine e;
    e.setState (root, scaleIndex, ScaleChordEngine::getPitchClassMask (root, scaleIndex));
    VoicePitchTracker t;
    t.prepare (sr);

    const int block = 512;
    const int total = (int) (seconds * sr);
    std::vector<float> buf ((size_t) block);
    double phase = 0.0;
    noteOns = noteOffs = 0;
    firstNote = -1; firstOnSample = -1;
    float lastHz = 0.0f;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        for (int i = 0; i < n; ++i)
        {
            const double tsec = (double) (pos + i) / sr;
            const double f = freq * std::pow (2.0, vibratoCents / 1200.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * tsec));
            phase += f / sr;
            phase -= std::floor (phase);
            double s = std::sin (2.0 * juce::MathConstants<double>::pi * phase);
            if (harmonic)   // timbre tipo voz: armónicos decrecientes
                s = 0.6 * s + 0.3 * std::sin (4.0 * juce::MathConstants<double>::pi * phase)
                    + 0.2 * std::sin (6.0 * juce::MathConstants<double>::pi * phase)
                    + 0.1 * std::sin (8.0 * juce::MathConstants<double>::pi * phase);
            buf[(size_t) i] = amplitude * (float) s;
        }
        const float* chans[1] = { buf.data() };
        const int ne = t.process (chans, 1, n, e);
        for (int k = 0; k < ne; ++k)
        {
            const auto& ev = t.getEvent (k);
            if (ev.isNoteOn)
            {
                if (firstNote < 0) { firstNote = ev.note; firstOnSample = pos + ev.sampleOffset; }
                ++noteOns;
            }
            else ++noteOffs;
        }
        if (t.detectedFrequency.load() > 0.0f) lastHz = t.detectedFrequency.load();
    }

    // Silencio final -> debe soltar la nota
    std::fill (buf.begin(), buf.end(), 0.0f);
    for (int k = 0; k < 40; ++k)
    {
        const float* chans[1] = { buf.data() };
        const int ne = t.process (chans, 1, block, e);
        for (int j = 0; j < ne; ++j)
            if (! t.getEvent (j).isNoteOn) ++noteOffs;
    }

    if (detectedHz) *detectedHz = lastHz;
    return lastHz;
}

static void testPitchTracker()
{
    std::printf ("[8] Detector de voz (YIN + gate + histéresis + estabilidad)\n");

    struct Case { double sr; float hz; int expected; const char* name; };
    const Case cases[] = {
        { 44100.0, 220.0f,   57, "A3 @44.1k" },
        { 48000.0, 440.0f,   69, "A4 @48k"   },
        { 96000.0, 261.63f,  60, "C4 @96k"   },
        { 48000.0, 65.41f,   36, "C2 @48k (límite grave)" },
        { 44100.0, 1046.5f,  84, "C6 @44.1k (límite agudo)" },
        { 88200.0, 146.83f,  50, "D3 @88.2k" },
    };

    for (const auto& c : cases)
    {
        int ons, offs, first, firstSample;
        float hz = 0.0f;
        runTracker (c.sr, c.hz, 0.25f, 1.0f, ons, offs, first, firstSample, 0.0f, false, 2, 0, &hz);
        const float centsErr = 1200.0f * std::log2 (hz / c.hz);
        const double latencyMs = 1000.0 * firstSample / c.sr;
        std::printf ("    %-26s nota=%d (esperada %d)  f0=%.2f Hz  error=%+.2f cents  latencia=%.1f ms  on=%d off=%d\n",
                     c.name, first, c.expected, hz, centsErr, latencyMs, ons, offs);
        CHECK (first == c.expected, "nota detectada correcta");
        CHECK (ons == 1 && offs == 1, "un solo NoteOn y un NoteOff");
        CHECK (std::abs (centsErr) < 5.0f, "error < 5 cents");
        CHECK (latencyMs < 60.0, "latencia de disparo < 60 ms");
    }

    // Timbre armónico (sin errores de octava)
    {
        int ons, offs, first, fs;
        runTracker (44100.0, 110.0f, 0.25f, 1.0f, ons, offs, first, fs, 0.0f, true);
        CHECK (first == 45 && ons == 1, "voz armónica 110 Hz -> A2 sin error de octava");
    }

    // Vibrato de ±30 cents no fragmenta la nota
    {
        int ons, offs, first, fs;
        runTracker (48000.0, 220.0f, 0.25f, 2.0f, ons, offs, first, fs, 30.0f);
        std::printf ("    vibrato ±30 cents: NoteOn=%d\n", ons);
        CHECK (ons == 1, "vibrato no genera notas fragmentadas");
    }

    // Nota entre dos grados (233 Hz ~ A#3) en C Major: sin parpadeo
    {
        int ons, offs, first, fs;
        runTracker (48000.0, 233.08f, 0.25f, 1.5f, ons, offs, first, fs, 15.0f, false, 0, 0);
        std::printf ("    A#3 en C Major con vibrato: nota=%d NoteOn=%d\n", first, ons);
        CHECK ((first == 57 || first == 59) && ons == 1, "cuantiza a la escala sin parpadear entre A y B");
    }

    // Por debajo del gate (-40 dB): sin eventos
    {
        int ons, offs, first, fs;
        runTracker (48000.0, 220.0f, 0.004f, 1.0f, ons, offs, first, fs);   // ~ -51 dB RMS
        CHECK (ons == 0, "señal bajo el umbral del gate no dispara notas");
    }
}

//==============================================================================
static float goertzel (const float* x, int n, double sr, double f)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1; s1 = s;
    }
    return (float) std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (float) n;
}

static void testSynth()
{
    std::printf ("[9] Sintetizador de piano interno\n");
    const double sr = 48000.0;
    const int block = 256;
    FlKeysPianoSynth synth;
    synth.prepare (sr, block);
    synth.setReferencePitch (440.0f);

    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> capture;
    capture.reserve ((size_t) sr * 2);
    juce::MidiBuffer midi;

    float peak = 0.0f;
    bool finite = true;
    const int blocks = (int) (1.5 * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : { 60, 64, 67 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
        if (b == (int) (1.0 * sr / block))
            for (int n : { 60, 64, 67 })
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        buf.clear();
        synth.render (buf, midi, 0, block);
        for (int i = 0; i < block; ++i)
        {
            const float l = buf.getSample (0, i), r = buf.getSample (1, i);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            capture.push_back (l);
        }
    }

    const int analysisStart = (int) (0.05 * sr), analysisLen = (int) (0.5 * sr);
    const float* a = capture.data() + analysisStart;
    const float c = goertzel (a, analysisLen, sr, 261.63), e = goertzel (a, analysisLen, sr, 329.63),
                g = goertzel (a, analysisLen, sr, 392.00), off = goertzel (a, analysisLen, sr, 300.0);
    std::printf ("    pico=%.3f  C=%.4f E=%.4f G=%.4f  (fuera de acorde 300 Hz=%.5f)  voces activas al final=%d\n",
                 peak, c, e, g, off, synth.getNumActiveVoices());
    CHECK (finite, "sin NaN/Inf");
    CHECK (peak > 0.05f && peak < 1.0f, "nivel audible sin clip");
    CHECK (c > off * 20 && e > off * 20 && g > off * 20, "el espectro contiene C-E-G");
    CHECK (synth.getNumActiveVoices() == 0, "las voces terminan tras el release");

    // Robo de voces sin memoria dinámica: 41 notas pulsadas con 32 voces
    {
        juce::MidiBuffer burst;
        for (int n = 40; n <= 80; ++n)
            burst.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), n - 40);
        juce::AudioBuffer<float> b2 (2, 512);
        b2.clear();
        long allocs = 0;
        {
            AllocationGuard guard;
            synth.render (b2, burst, 0, 512);
            allocs = guard.count();
        }
        bool lowAndTop = false, bothFound[2] = { false, false };
        auto& sy = synth.getSynthesiser();
        for (int i = 0; i < sy.getNumVoices(); ++i)
        {
            const int note = sy.getVoice (i)->getCurrentlyPlayingNote();
            if (note == 40) bothFound[0] = true;
            if (note == 80) bothFound[1] = true;
        }
        lowAndTop = bothFound[0] && bothFound[1];
        CHECK (synth.getNumActiveVoices() == FlKeysPianoSynth::kNumVoices, "41 notas: las 32 voces suenan (se roban las más antiguas)");
        CHECK (lowAndTop, "la nota más grave y la más aguda pulsadas se protegen del robo");
        CHECK (allocs == 0, "robar voces no pide memoria (StealSafeSynthesiser)");
        synth.allNotesOff();
    }
}

//==============================================================================
static void testChordNamer()
{
    std::printf ("[11] Nombrador de acordes con extensiones\n");
    ScaleChordEngine e;   // C Major -> sostenidos

    struct Case { std::vector<int> notes; const char* expected; };
    const Case cases[] =
    {
        { { 48, 52, 55 },             "C Maj" },
        { { 48, 51, 55 },             "Cm" },
        { { 48, 51, 54 },             "C dim" },
        { { 48, 52, 56 },             "C aug" },
        { { 48, 53, 55 },             "Csus4" },
        { { 48, 50, 55 },             "Csus2" },
        { { 48, 52, 55, 59 },         "C Maj7" },
        { { 48, 52, 55, 59, 62 },     "C Maj9" },
        { { 48, 52, 55, 59, 62, 69 }, "C Maj13" },
        { { 48, 52, 55, 59, 62, 66 }, "C Maj9#11" },
        { { 48, 52, 55, 58 },         "C7" },
        { { 48, 52, 55, 58, 62 },     "C9" },
        { { 48, 52, 55, 58, 62, 69 }, "C13" },
        { { 48, 52, 55, 58, 61 },     "C7b9" },
        { { 48, 51, 55, 58 },         "Cm7" },
        { { 48, 51, 55, 58, 62 },     "Cm9" },
        { { 48, 51, 55, 58, 62, 65 }, "Cm11" },
        { { 48, 51, 55, 58, 65 },     "Cm7(11)" },
        { { 48, 51, 54, 58 },         "Cm7b5" },
        { { 48, 51, 54, 58, 65 },     "Cm7b5(11)" },
        { { 48, 51, 54, 57 },         "Cdim7" },
        { { 48, 52, 55, 62 },         "Cadd9" },
        { { 48, 51, 55, 62 },         "Cm(add9)" },
        { { 48, 51, 55, 65 },         "Cm(add11)" },
        { { 48, 52, 55, 57 },         "C6" },
        { { 48, 52, 55, 57, 62 },     "C6/9" },
        { { 48, 53, 55, 58 },         "C7sus4" },
        { { 48, 53, 55, 58, 62 },     "C9sus4" },
        { { 48, 51, 55, 59 },         "CmMaj7" },
        { { 48, 55 },                 "C5" },
        { { 52, 55, 59, 60 },         "C Maj7/E" },
        { { 57, 60, 64, 67 },         "Am7" },
        { { 48, 53, 57 },             "F Maj/C" },
        { { 48, 52 },                 "C(no5)" },
        { { 60 },                     "C4" },
    };

    int ok = 0;
    for (const auto& c : cases)
    {
        const auto name = e.getChordName (c.notes);
        ++gChecks;
        if (name == c.expected) ++ok;
        else
        {
            ++gFailures;
            std::printf ("  [FALLO] esperado \"%s\", obtenido \"%s\"\n", c.expected, name.toRawUTF8());
        }
    }
    std::printf ("    %d/%d cifrados correctos\n", ok, (int) (sizeof (cases) / sizeof (cases[0])));
}

//==============================================================================
static juce::String voicingToString (const ScaleChordEngine::Voicing& v)
{
    juce::String s;
    for (int i = 0; i < v.count; ++i) s << v.notes[(size_t) i] << " ";
    return s.trim();
}

static std::vector<int> voicingNotes (const ScaleChordEngine::Voicing& v)
{
    return std::vector<int> (v.notes.begin(), v.notes.begin() + v.count);
}

static void testComplexChords()
{
    std::printf ("[12] COMPLEX CHORDS en Fa Mayor (los 7 grados x 3 variaciones)\n");
    ScaleChordEngine e;
    e.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));
    e.setMode (OperationMode::ComplexChord);

    struct Expect { ComplexVariation v; int key; const char* name; const char* notes; };
    const Expect cases[] =
    {
        // 7th & 9th Lush: M.I. raíz-12 + 5ª-12 | M.D. raíz, 3ª, 7ª, 9ª, 5ª arriba
        { ComplexVariation::LushSevenNine, 65, "F Maj9",     "53 60 65 69 76 79 84" },
        { ComplexVariation::LushSevenNine, 67, "Gm9",        "55 62 67 70 77 81 86" },
        { ComplexVariation::LushSevenNine, 69, "Am7(11)",    "57 64 69 72 79 86 88" },
        { ComplexVariation::LushSevenNine, 70, "Bb Maj9",    "58 65 70 74 81 84 89" },
        { ComplexVariation::LushSevenNine, 60, "C9",         "48 55 60 64 70 74 79" },
        { ComplexVariation::LushSevenNine, 62, "Dm9",        "50 57 62 65 72 76 81" },
        { ComplexVariation::LushSevenNine, 64, "Em7b5",      "52 58 64 67 74 76 82" },
        // 9th & 11th Neo-Soul: M.D. raíz, 3ª, 7ª, 9ª, 11ª (11ª sobre 3ª mayor -> 13ª)
        { ComplexVariation::NeoSoulNineEleven, 65, "F Maj13",    "53 60 65 69 76 79 86" },
        { ComplexVariation::NeoSoulNineEleven, 67, "Gm11",       "55 62 67 70 77 81 84" },
        { ComplexVariation::NeoSoulNineEleven, 69, "Am7(11)",    "57 64 69 72 79 81 86" },
        { ComplexVariation::NeoSoulNineEleven, 70, "Bb Maj9#11", "58 65 70 74 81 84 88" },
        { ComplexVariation::NeoSoulNineEleven, 60, "C13",        "48 55 60 64 70 74 81" },
        { ComplexVariation::NeoSoulNineEleven, 62, "Dm11",       "50 57 62 65 72 76 79" },
        { ComplexVariation::NeoSoulNineEleven, 64, "Em7b5(11)",  "52 58 64 67 74 76 81" },
        // Cinematic Spread Add9: M.D. raíz, 9ª, 10ª, 12ª, raíz +2 octavas
        { ComplexVariation::CinematicAdd9, 65, "Fadd9",      "53 60 65 79 81 84 89" },
        { ComplexVariation::CinematicAdd9, 67, "Gm(add9)",   "55 62 67 82 86 91 93" },
        { ComplexVariation::CinematicAdd9, 69, "Am(add11)",  "57 64 69 84 86 88 93" },
        { ComplexVariation::CinematicAdd9, 70, "Bbadd9",     "58 65 70 84 86 89 94" },
        { ComplexVariation::CinematicAdd9, 60, "Cadd9",      "48 55 60 74 76 79 84" },
        { ComplexVariation::CinematicAdd9, 62, "Dm(add9)",   "50 57 62 77 81 86 88" },
        { ComplexVariation::CinematicAdd9, 64, "E dim",      "52 58 64 76 79 82 88" },
    };

    const char* varNames[3] = { "Lush", "Neo-Soul", "Cinematic" };
    for (const auto& c : cases)
    {
        e.setComplexVariation (c.v);
        ScaleChordEngine::Voicing v;
        e.generateVoicing (c.key, v);
        const auto name = e.getChordName (voicingNotes (v));
        const auto notes = voicingToString (v);
        ++gChecks;
        if (name != c.name || notes != c.notes)
        {
            ++gFailures;
            std::printf ("  [FALLO] %s %d: esperado %s [%s], obtenido %s [%s]\n", varNames[(int) c.v], c.key,
                         c.name, c.notes, name.toRawUTF8(), notes.toRawUTF8());
        }
    }

    // Impresión de la progresión I..vii en cada variación
    for (int var = 0; var < 3; ++var)
    {
        e.setComplexVariation ((ComplexVariation) var);
        juce::String line;
        for (int key : { 65, 67, 69, 70, 60, 62, 64 })
        {
            ScaleChordEngine::Voicing v;
            e.generateVoicing (key, v);
            line << e.getChordName (voicingNotes (v)) << " (" << v.count << ")  ";
        }
        std::printf ("    %-10s %s\n", varNames[var], line.toRawUTF8());
    }

    // Velocity voicing: mano izquierda 95 %, voces centrales 80 %, voz aguda 90 %
    e.setComplexVariation (ComplexVariation::NeoSoulNineEleven);
    ScaleChordEngine::Voicing v;
    e.generateVoicing (62, v);
    bool velOk = v.count == 7;
    for (int i = 0; i < v.count; ++i)
    {
        const float expected = i < 2 ? 0.95f : (i == v.count - 1 ? 0.90f : 0.80f);
        velOk = velOk && std::abs (v.velocityScale[(size_t) i] - expected) < 1.0e-6f;
    }
    CHECK (velOk, "velocity voicing 95 % / 80 % / 90 %");

    // La mano izquierda siempre es raíz-12 y 5ª diatónica-12
    e.setComplexVariation (ComplexVariation::LushSevenNine);
    e.generateVoicing (62, v);
    CHECK (v.notes[0] == 50 && v.notes[1] == 57, "M.I. de Re: D3 + A3");

    // Teclas fuera de escala se cuantizan antes de construir el acorde
    ScaleChordEngine::Voicing v2;
    e.generateVoicing (71, v2);   // B natural en Fa Mayor -> Bb
    e.generateVoicing (70, v);
    CHECK (voicingToString (v) == voicingToString (v2), "B natural -> acorde de Bb");

    // Guarda de registro en los extremos: siempre 5-7 notas dentro de 0..127
    for (int key : { 0, 5, 120, 127 })
    {
        e.generateVoicing (key, v);
        bool inRange = v.count >= 5;
        for (int i = 0; i < v.count; ++i) inRange = inRange && v.notes[(size_t) i] >= 0 && v.notes[(size_t) i] <= 127;
        CHECK (inRange, "voicing completo y en rango en los extremos del teclado");
    }
}

static void testComplexExhaustive()
{
    std::printf ("[13] Exhaustivo COMPLEX: 43 escalas x 12 tónicas x 128 notas x 3 variaciones\n");
    ScaleChordEngine e;
    e.setMode (OperationMode::ComplexChord);
    long combos = 0;
    int badCount = 0, badScale = 0, badOrder = 0, clashes = 0, hist[9] = {};

    for (int s = 0; s < kNumScales; ++s)
        for (int root = 0; root < 12; ++root)
        {
            const auto mask = ScaleChordEngine::getPitchClassMask (root, s);
            e.setState (root, s, mask);
            const bool hepta = e.getNumActivePitchClasses() == 7;

            for (int var = 0; var < 3; ++var)
            {
                e.setComplexVariation ((ComplexVariation) var);
                for (int n = 0; n < 128; ++n)
                {
                    ScaleChordEngine::Voicing v;
                    e.generateVoicing (n, v);
                    ++combos;
                    hist[juce::jlimit (0, 8, v.count)]++;

                    if (v.count < 5 || v.count > 7) ++badCount;
                    for (int i = 0; i < v.count; ++i)
                    {
                        const int note = v.notes[(size_t) i];
                        if (note < 0 || note > 127 || ((mask >> (note % 12)) & 1u) == 0) ++badScale;
                        if (i > 0 && note <= v.notes[(size_t) (i - 1)]) ++badOrder;
                        if (hepta)
                            for (int j = 0; j < i; ++j)
                            {
                                const int d = note - v.notes[(size_t) j];
                                if (d == 1 || d == 13) ++clashes;
                            }
                    }
                }
            }
        }

    std::printf ("    %ld voicings | 5 notas: %d  6 notas: %d  7 notas: %d\n", combos, hist[5], hist[6], hist[7]);
    CHECK (badCount == 0, "siempre entre 5 y 7 notas");
    CHECK (badScale == 0, "todas las notas en la escala y en 0..127");
    CHECK (badOrder == 0, "notas ordenadas y sin duplicados");
    CHECK (clashes == 0, "escalas heptatónicas: ninguna 2ª menor ni 9ª menor entre voces");
}

static void testComplexRouterAndVoice()
{
    std::printf ("[14] COMPLEX: Piano Roll (MIDI out), NoteOff exactos y disparo por voz\n");
    ScaleChordEngine e;
    e.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));
    e.setMode (OperationMode::ComplexChord);
    e.setComplexVariation (ComplexVariation::NeoSoulNineEleven);

    MidiOutputRouter router;
    juce::MidiBuffer out;
    out.ensureSize (8192);

    router.handleNoteOn (MidiOutputRouter::Source::Midi, 62, 1, 100, 0, e, out);
    std::vector<int> ons, vels;
    for (const auto m : out)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { ons.push_back (msg.getNoteNumber()); vels.push_back (msg.getVelocity()); }
    }
    CHECK (vecToString (ons) == "50 57 62 65 72 76 79", "1 tecla -> 7 NoteOn (Dm11) hacia el Piano Roll");
    CHECK (vecToString (vels) == "95 95 80 80 80 80 90", "velocidades por voz: 95/80/90 % de 100");

    // Cambio de variación y de modo con la tecla pulsada: el NoteOff libera las 7 notas exactas
    e.setComplexVariation (ComplexVariation::CinematicAdd9);
    e.setMode (OperationMode::SingleNote);
    out.clear();
    router.handleNoteOff (MidiOutputRouter::Source::Midi, 62, 5, out);
    std::vector<int> offs;
    for (const auto m : out)
        if (m.getMessage().isNoteOff()) offs.push_back (m.getMessage().getNoteNumber());
    CHECK (vecToString (offs) == "50 57 62 65 72 76 79", "NoteOff exactos aunque cambió el modo");
    uint64_t lo, hi;
    router.getActiveBitmask (lo, hi);
    CHECK (lo == 0 && hi == 0, "nada queda colgado");

    const auto ev = MidiOutputRouter::decodeLastEvent (router.lastEventPacked.load());
    CHECK (ev.valid && ev.notes.size() == 7 && ev.mode == 2 && ev.triggerNote == 62, "display: 7 notas + modo empaquetados");

    // Voz -> acorde complejo: tarareo de A3 (220 Hz) en Fa Mayor
    e.setMode (OperationMode::ComplexChord);
    e.setComplexVariation (ComplexVariation::LushSevenNine);
    VoicePitchTracker t;
    t.prepare (48000.0);
    std::vector<float> buf (512);
    int voiceOns = 0;
    std::vector<int> voiceNotes;
    double phase = 0.0;
    for (int b = 0; b < 100; ++b)
    {
        for (auto& x : buf)
        {
            phase += 220.0 / 48000.0;
            phase -= std::floor (phase);
            x = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
        }
        const float* chans[1] = { buf.data() };
        out.clear();
        const int ne = t.process (chans, 1, (int) buf.size(), e);
        for (int k = 0; k < ne; ++k)
        {
            const auto& tev = t.getEvent (k);
            if (tev.isNoteOn) router.handleNoteOn (MidiOutputRouter::Source::Voice, tev.note, 1, (juce::uint8) tev.velocity, tev.sampleOffset, e, out);
            else              router.handleNoteOff (MidiOutputRouter::Source::Voice, tev.note, tev.sampleOffset, out);
        }
        for (const auto m : out)
            if (m.getMessage().isNoteOn()) { ++voiceOns; voiceNotes.push_back (m.getMessage().getNoteNumber()); }
    }
    std::printf ("    voz A3 -> %s (%d notas)\n", e.getChordName (voiceNotes).toRawUTF8(), voiceOns);
    CHECK (voiceOns == 7 && vecToString (voiceNotes) == "45 52 57 60 67 74 76", "tararear A3 dispara el acorde complejo completo (Am7(11))");
}

static void testComplexPolyphonyAndLevel()
{
    std::printf ("[15] Polifonía (>= 24 voces sin cortes) y nivel con acordes de 7 notas\n");
    const double sr = 48000.0;
    const int block = 256;
    FlKeysPianoSynth synth;
    synth.prepare (sr, block);
    CHECK (synth.getSynthesiser().getNumVoices() >= 24, "al menos 24 voces");

    ScaleChordEngine e;
    e.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));
    e.setMode (OperationMode::ComplexChord);
    e.setComplexVariation (ComplexVariation::NeoSoulNineEleven);

    MidiOutputRouter router;
    juce::MidiBuffer midi;
    midi.ensureSize (8192);
    juce::AudioBuffer<float> buf (2, block);

    // a) Capacidad pura: 28 notas distintas a la vez (4 acordes de 7) -> 28 voces sin robo
    {
        FlKeysPianoSynth s;
        s.prepare (sr, block);
        juce::MidiBuffer m28;
        for (int n = 36; n < 64; ++n)
            m28.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        buf.clear();
        s.render (buf, m28, 0, block);
        std::printf ("    28 notas distintas simultáneas -> %d voces sonando\n", s.getNumActiveVoices());
        CHECK (s.getNumActiveVoices() == 28, "28 notas simultáneas sin robar voces (mínimo exigido: 24)");
    }

    // b) 4 acordes complejos reales mantenidos a la vez (las notas compartidas se cuentan una vez)
    for (int key : { 41, 57, 72, 88 })   // F2, A3, C5, E6
        router.handleNoteOn (MidiOutputRouter::Source::Midi, key, 1, 127, 0, e, midi);
    int requested = 0;
    for (const auto m : midi)
        if (m.getMessage().isNoteOn()) ++requested;

    float peak = 0.0f;
    buf.clear();
    synth.render (buf, midi, 0, block);
    const int activeAfterOn = synth.getNumActiveVoices();
    for (int b = 0; b < 40; ++b)
    {
        midi.clear();
        if (b > 0) buf.clear();
        if (b > 0) synth.render (buf, midi, 0, block);
        peak = std::max (peak, buf.getMagnitude (0, block));
    }
    std::printf ("    notas pedidas=%d  voces sonando=%d  pico=%.3f\n", requested, activeAfterOn, peak);
    CHECK (requested >= 20 && activeAfterOn == requested, "4 acordes complejos sostenidos sin robar voces");
    CHECK (peak < 1.0f, "sin clip con 4 acordes complejos a velocidad 127");

    // Un acorde de 7 notas a velocidad máxima
    FlKeysPianoSynth synth2;
    synth2.prepare (sr, block);
    MidiOutputRouter r2;
    midi.clear();
    r2.handleNoteOn (MidiOutputRouter::Source::Midi, 62, 1, 127, 0, e, midi);
    float peak7 = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        buf.clear();
        synth2.render (buf, midi, 0, block);
        midi.clear();
        peak7 = std::max (peak7, buf.getMagnitude (0, block));
    }
    std::printf ("    acorde de 7 notas a vel 127: pico=%.3f\n", peak7);
    CHECK (peak7 > 0.1f && peak7 < 1.0f, "nivel audible y sin clip");
}
//==============================================================================
static void testStrumScheduler()
{
    std::printf ("[19] STRUM: entrada progresiva por muestra, ASC/DESC y NoteOff anticipado\n");
    const double sr = 48000.0;
    const int block = 512;

    struct Hit { int64_t time; int note; bool on; };
    auto runCase = [&] (double ms, bool desc, int64_t releaseAt, std::vector<Hit>& hits, uint64_t* midMask = nullptr, uint64_t* midMaskHi = nullptr)
    {
        StrumScheduler s;
        s.prepare (sr);
        s.setStrum (ms, desc);
        juce::MidiBuffer in, out;
        for (int b = 0; b < 40; ++b)
        {
            in.clear();
            out.clear();
            const int64_t start = (int64_t) b * block;
            if (b == 0)
                for (int n : { 64, 60, 67 })
                    in.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 10);
            if (releaseAt >= start && releaseAt < start + block)
                for (int n : { 60, 64, 67 })
                    in.addEvent (juce::MidiMessage::noteOff (1, n), (int) (releaseAt - start));
            s.process (in, out, block);
            for (const auto meta : out)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOnOrOff())
                    hits.push_back ({ start + meta.samplePosition, m.getNoteNumber(), m.isNoteOn() });
            }
            if (midMask != nullptr && midMaskHi != nullptr && b == 15)
                s.getSoundingBitmask (*midMask, *midMaskHi);
        }
    };

    auto onTime = [] (const std::vector<Hit>& hits, int note) -> int64_t
    {
        for (const auto& h : hits) if (h.on && h.note == note) return h.time;
        return -1;
    };
    auto offTime = [] (const std::vector<Hit>& hits, int note) -> int64_t
    {
        for (const auto& h : hits) if (! h.on && h.note == note) return h.time;
        return -1;
    };

    // ASC 100 ms: 0 / 115 ms / 260 ms
    std::vector<Hit> asc;
    uint64_t midMask = 0, midMaskHi = 0;
    runCase (100.0, false, 19000, asc, &midMask, &midMaskHi);
    std::printf ("    ASC 100 ms: C4 %lld · E4 %lld · G4 %lld muestras\n",
                 (long long) onTime (asc, 60), (long long) onTime (asc, 64), (long long) onTime (asc, 67));
    CHECK (onTime (asc, 60) == 10 && onTime (asc, 64) == 10 + 5520 && onTime (asc, 67) == 10 + 12480,
           "ASC: grave -> aguda con t_i = i·base·(1+0.15·i) exacto a la muestra");
    CHECK (offTime (asc, 60) == 19000 && offTime (asc, 64) == 19000 && offTime (asc, 67) == 19000,
           "los NoteOff llegan todos juntos al soltar (final alineado)");
    CHECK (((midMask >> 60) & 1u) && ((midMaskHi >> (64 - 64)) & 1u) && ! ((midMaskHi >> (67 - 64)) & 1u),
           "el teclado se ilumina nota a nota (a mitad del barrido suenan 2 de 3)");

    std::vector<Hit> descHits;
    runCase (100.0, true, 19000, descHits);
    CHECK (onTime (descHits, 67) == 10 && onTime (descHits, 64) == 10 + 5520 && onTime (descHits, 60) == 10 + 12480,
           "DESC: aguda -> grave");

    // Soltar antes de que termine el barrido: lo pendiente se cancela, nada queda colgado
    std::vector<Hit> early;
    runCase (100.0, false, 3000, early);
    CHECK (onTime (early, 60) == 10 && offTime (early, 60) == 3000 && onTime (early, 64) < 0 && onTime (early, 67) < 0
           && offTime (early, 64) < 0 && offTime (early, 67) < 0,
           "NoteOff anticipado: cancela las notas pendientes y apaga la que sonaba");

    std::vector<Hit> block0;
    runCase (0.0, false, 19000, block0);
    CHECK (onTime (block0, 60) == 10 && onTime (block0, 64) == 10 && onTime (block0, 67) == 10,
           "STRUM en 0 ms: el acorde entra en bloque");

    // Pánico (All Notes Off) cancela lo pendiente
    StrumScheduler p;
    p.prepare (sr);
    p.setStrum (200.0, false);
    juce::MidiBuffer in, out;
    in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    in.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    p.process (in, out, block);
    in.clear();
    in.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    p.process (in, out, block);
    CHECK (p.getNumPending() == 0, "ALL NOTES OFF vacía la cola del STRUM");
}
//==============================================================================
// Las pruebas del búfer escriben números enteros: la comparación es exacta
static bool sameSample (float a, float b) { return std::abs (a - b) < 0.25f; }

static void testMicRingBuffer()
{
    std::printf ("[17] Búfer circular del micrófono (lock-free, memoria fija)\n");

    // 1) Canal 1 de un bloque estéreo intercalado
    {
        MicRingBuffer<1024> ring;
        std::vector<float> stereo (600);
        for (int i = 0; i < 300; ++i)
        {
            stereo[(size_t) (2 * i)] = (float) i;
            stereo[(size_t) (2 * i + 1)] = -1000.0f - (float) i;
        }
        std::vector<float> out (1024);
        CHECK (ring.write (stereo.data(), 300, 2) == 300, "escribe 300 tramas estéreo");
        const int n = ring.read (out.data(), 1024, 0);
        CHECK (n == 300 && sameSample (out[0], 0.0f) && sameSample (out[150], 150.0f) && sameSample (out[299], 299.0f),
               "lee sólo el canal 1 (las muestras del canal 2 no aparecen)");
    }

    // 2) Miles de vueltas con tamaños irregulares: nada se pierde ni se repite
    //    (como mucho 3 x 600 pendientes: nunca se llena)
    {
        MicRingBuffer<4096> ring;
        std::vector<float> in (700), out (1024);
        float writeValue = 0.0f, expected = 0.0f;
        bool ok = true;
        AllocationGuard guard;
        for (int round = 0; round < 20000; ++round)
        {
            const int k = 1 + (round * 37) % 600;
            for (int i = 0; i < k; ++i)
                in[(size_t) i] = writeValue++;
            ring.write (in.data(), k, 1);
            const int got = ring.read (out.data(), 1 + (round * 53) % 1024, 0);
            for (int i = 0; i < got; ++i)
                ok = ok && sameSample (out[(size_t) i], expected++);
            if (round % 3 == 0)
            {
                const int rest = ring.read (out.data(), 1024, 0);
                for (int i = 0; i < rest; ++i)
                    ok = ok && sameSample (out[(size_t) i], expected++);
            }
        }
        CHECK (ok, "20000 escrituras y lecturas de tamaños irregulares: secuencia exacta");
        CHECK (guard.count() == 0, "escribir y leer nunca asigna memoria");
    }

    // 3) Desbordamiento (el DAW dejó de pedir audio): se tira lo viejo, sigue lo nuevo
    {
        MicRingBuffer<1024> ring;
        std::vector<float> big (1500), out (1024);
        for (int i = 0; i < 1500; ++i) big[(size_t) i] = (float) i;
        CHECK (ring.write (big.data(), 1500, 1) == 1024, "sólo caben 1024 muestras");
        CHECK (ring.read (out.data(), 1024, 0) == 0, "tras desbordarse, el lector descarta el audio viejo");
        for (int i = 0; i < 100; ++i) big[(size_t) i] = 5000.0f + (float) i;
        ring.write (big.data(), 100, 1);
        const int n = ring.read (out.data(), 1024, 0);
        CHECK (n == 100 && sameSample (out[0], 5000.0f) && sameSample (out[99], 5099.0f), "y sigue con el audio nuevo, sin saltos");
    }

    // 4) Latencia acotada: con mucho pendiente sólo se lee lo más reciente
    {
        MicRingBuffer<1024> ring;
        std::vector<float> in (900), out (1024);
        for (int i = 0; i < 900; ++i) in[(size_t) i] = (float) i;
        ring.write (in.data(), 900, 1);
        const int n = ring.read (out.data(), 1024, 256);
        CHECK (n == 256 && sameSample (out[0], 644.0f) && sameSample (out[255], 899.0f), "900 pendientes con límite 256: las 256 más recientes");
        CHECK (ring.getNumReady() == 0, "y no queda nada atrasado");
    }

    // 5) Dos hilos de verdad: productor a ráfagas (como CoreAudio) y consumidor a otro ritmo
    {
        MicRingBuffer<16384> ring;
        std::atomic<bool> stop { false };
        std::atomic<long> produced { 0 };
        std::thread producer ([&]
        {
            std::vector<float> chunk (2 * 512);
            float value = 0.0f;
            int round = 0;
            while (! stop.load())
            {
                const int frames = 64 + (round++ * 97) % 449;
                for (int i = 0; i < frames; ++i)
                {
                    chunk[(size_t) (2 * i)] = value++;
                    chunk[(size_t) (2 * i + 1)] = -1.0f;
                }
                produced += ring.write (chunk.data(), frames, 2);
                std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
        });

        std::vector<float> out (16384);
        float expected = 0.0f;
        long consumed = 0;
        bool ok = true;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (1500);
        while (std::chrono::steady_clock::now() < end)
        {
            const int n = ring.read (out.data(), 16384, 0);
            for (int i = 0; i < n; ++i)
                ok = ok && sameSample (out[(size_t) i], expected++);
            consumed += n;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        stop = true;
        producer.join();
        consumed += ring.read (out.data(), 16384, 0);
        std::printf ("    2 hilos durante 1.5 s: %ld muestras producidas, %ld leídas\n", produced.load(), consumed);
        CHECK (ok && consumed > 10000, "productor y consumidor simultáneos: secuencia exacta, sin candados");
    }
}

//==============================================================================
// Micrófono del sistema: el control (permiso, dispositivo, reaperturas) con el
// backend SIMULADO de Linux, que entrega audio a tiempo real desde su hilo.
#if JUCE_LINUX
static void setEnv (const char* name, const char* value)
{
    if (value == nullptr) unsetenv (name);
    else                  setenv (name, value, 1);
}
#endif

static void testSystemMicController()
{
   #if JUCE_LINUX
    std::printf ("[18] Micrófono del sistema: apertura, permisos y vigilancia (simulado)\n");

    setEnv ("SMARTKEYS_FAKE_MIC_PERMISSION", nullptr);
    setEnv ("SMARTKEYS_FAKE_MIC", nullptr);
    {
        SystemMicInput mic;
        mic.update (true);
        CHECK (mic.getState() == MicDeviceState::Unsupported && ! mic.isRunning(),
               "sin lectura directa disponible: no se abre nada");
    }

    setEnv ("SMARTKEYS_FAKE_MIC", "sine:220:44100:0.3");
    SystemMicInput mic;

    mic.update (false);
    CHECK (mic.getState() == MicDeviceState::Off && ! mic.isRunning(), "MIC VOICE apagado: el micrófono queda cerrado");

    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::Running && mic.isRunning() && mic.getGeneration() == 1,
           "MIC VOICE encendido: se abre la entrada");
    CHECK (std::abs (mic.getSampleRate() - 44100.0) < 0.5, "a la frecuencia del propio dispositivo (44.1 kHz), sin cambiarla");

    // Leer como processBlock: el detector a la frecuencia del micrófono
    {
        VoicePitchTracker tracker;
        ScaleChordEngine engine;
        tracker.prepare (mic.getSampleRate());
        tracker.setGateThresholdDb (-60.0f);
        std::vector<float> scratch ((size_t) SystemMicInput::kRingCapacity);
        int total = 0, firstNote = -1;
        for (int i = 0; i < 70; ++i)   // ~0.7 s
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            const int n = mic.pull (scratch.data(), (int) scratch.size(), 16000);
            total += n;
            const float* ch[1] = { scratch.data() };
            const int ev = tracker.process (ch, 1, n, engine);
            for (int e = 0; e < ev; ++e)
                if (tracker.getEvent (e).isNoteOn && firstNote < 0)
                    firstNote = tracker.getEvent (e).note;
        }
        std::printf ("    0.7 s de micrófono simulado: %d muestras, nota %d\n", total, firstNote);
        CHECK (total > 20000 && total < 40000, "llegan las muestras a tiempo real (~44 100 por segundo)");
        CHECK (firstNote == 57, "La3 (220 Hz, canal 1) se detecta; el tono de 330 Hz del canal 2 se ignora");
    }

    mic.update (false);
    CHECK (mic.getState() == MicDeviceState::Off && ! mic.isRunning(), "al apagar MIC VOICE se cierra");

    // Cambio de entrada en macOS (otra interfaz / otra frecuencia): se reabre sola
    mic.update (true);
    const auto generationBefore = mic.getGeneration();
    setEnv ("SMARTKEYS_FAKE_MIC", "sine:220:48000:0.3");
    mic.update (true);
    CHECK (mic.getGeneration() == generationBefore + 1 && std::abs (mic.getSampleRate() - 48000.0) < 0.5,
           "si cambia la entrada activa, se reabre con su frecuencia (48 kHz)");

    // El dispositivo deja de entregar audio: se reabre tras ~2 s
    setEnv ("SMARTKEYS_FAKE_MIC", "stall:48000");
    mic.update (true);
    const auto stalledGeneration = mic.getGeneration();
    for (int i = 0; i <= SystemMicInput::kStallTicks; ++i)
        mic.update (true);
    CHECK (mic.getGeneration() > stalledGeneration, "una entrada que deja de entregar audio se vuelve a abrir");

    // No se puede abrir: aviso y reintento espaciado (sin bucles rápidos)
    setEnv ("SMARTKEYS_FAKE_MIC", "fail");
    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::Error && ! mic.isRunning(), "si no se puede abrir: estado de error, nada abierto");

    setEnv ("SMARTKEYS_FAKE_MIC", "bluetooth");
    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::BluetoothOnly && ! mic.isRunning(),
           "una entrada Bluetooth (AirPods) no se abre");

    setEnv ("SMARTKEYS_FAKE_MIC", "none");
    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::NoDevice, "sin ninguna entrada de audio: aviso");

    // Permisos de macOS
    setEnv ("SMARTKEYS_FAKE_MIC", "sine:220:48000:0.3");
    setEnv ("SMARTKEYS_FAKE_MIC_PERMISSION", "denied");
    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::PermissionDenied && ! mic.isRunning(),
           "permiso denegado: no se abre y se avisa");

    setEnv ("SMARTKEYS_FAKE_MIC_PERMISSION", "nohost");
    mic.update (true);
    CHECK (mic.getState() == MicDeviceState::HostNotAllowed && ! mic.isRunning(),
           "programa sin NSMicrophoneUsageDescription: el micrófono no se toca");

    {
        setEnv ("SMARTKEYS_FAKE_MIC_PERMISSION", "pending");
        SystemMicInput fresh;
        fresh.update (true);
        CHECK (fresh.getState() == MicDeviceState::WaitingPermission && ! fresh.isRunning(),
               "primera vez: se muestra el aviso de macOS y se espera");
        std::this_thread::sleep_for (std::chrono::milliseconds (1150));
        fresh.update (true);
        CHECK (fresh.getState() == MicDeviceState::Running && fresh.isRunning(),
               "al permitirlo, el micrófono se abre solo");
    }

    setEnv ("SMARTKEYS_FAKE_MIC_PERMISSION", nullptr);
    mic.shutdown();
    CHECK (! mic.isRunning() && mic.getState() == MicDeviceState::Off, "shutdown() lo cierra todo");
    setEnv ("SMARTKEYS_FAKE_MIC", nullptr);
   #endif
}

//==============================================================================
//==============================================================================
// Teclado MIDI virtual "SmartKeys Chords": hilo de envío, orden y hora exacta,
// lo que saben todas las instancias, notas colgadas. Linux: escribe en un archivo.
#if JUCE_LINUX
struct PortLine { double ms; int status, d1, d2; };

static std::vector<PortLine> readPortFile (const juce::File& file)
{
    std::vector<PortLine> v;
    if (auto* f = std::fopen (file.getFullPathName().toRawUTF8(), "r"))
    {
        PortLine l {};
        while (std::fscanf (f, "%lf %d %d %d", &l.ms, &l.status, &l.d1, &l.d2) == 4)
            v.push_back (l);
        std::fclose (f);
    }
    return v;
}
#endif

static void testChordPort()
{
   #if JUCE_LINUX
    std::printf ("[20] Teclado MIDI virtual \"SmartKeys Chords\": orden, hora exacta y notas colgadas\n");

    ChordMidiPort::setStandaloneProcess (true);
    CHECK (ChordMidiPort::getDeviceName() == "SmartKeys Chords (App)", "la app Standalone usa otro nombre (sin choque con el DAW)");
    ChordMidiPort::setStandaloneProcess (false);
    CHECK (ChordMidiPort::getDeviceName() == "SmartKeys Chords", "en el DAW: \"SmartKeys Chords\"");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("smartkeys_port_unit.txt");
    file.deleteFile();
    setEnv ("SMARTKEYS_TEST_CHORD_PORT", file.getFullPathName().toRawUTF8());
    {
        ChordMidiPort port;
        CHECK (port.isAvailable(), "el teclado se abre");

        ChordPortFeed a, b, unconnected;
        port.addFeed (a);
        port.addFeed (b);

        // A: Fa mayor con STRUM DESC (C4 ya, A3 +40 ms, F3 +90 ms), metido desordenado
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        a.push ({ t0 + 90.0, 0x90, 53, 100 });
        a.push ({ t0 + 5.0,  0x90, 60, 100 });
        a.push ({ t0 + 40.0, 0x90, 57, 100 });
        // Un NoteOn que llega con 1 s de retraso (sistema bloqueado): no sale, ni su NoteOff
        a.push ({ t0 - 1000.0, 0x90, 70, 100 });
        a.push ({ t0 + 20.0,   0x80, 70, 0 });
        // B (otra instancia): una nota que queda sonando
        b.push ({ t0 + 10.0, 0x90, 48, 90 });

        std::this_thread::sleep_for (std::chrono::milliseconds (250));
        auto lines = readPortFile (file);
        std::vector<int> order;
        bool timingOk = lines.size() == 4;
        const double expected[] = { 5.0, 10.0, 40.0, 90.0 };
        for (size_t i = 0; i < lines.size(); ++i)
        {
            order.push_back (lines[i].d1);
            if (i < 4)
                timingOk = timingOk && std::abs (lines[i].ms - t0 - expected[i]) < 6.0;
        }
        CHECK ((order == std::vector<int> { 60, 48, 57, 53 }), "salen en el orden de su hora (C4, la otra instancia, A3, F3), sin el NoteOn atrasado");
        CHECK (timingOk, "cada nota sale a su hora (±6 ms): se conserva el STRUM");
        CHECK (a.getNoteOnSerial (60) == 1 && b.getNoteOnSerial (60) == 1 && a.getNoteOnSerial (48) == 1,
               "todas las instancias saben qué salió por el teclado (para reconocer el eco)");
        CHECK (a.getNoteOnSerial (70) == 0 && a.getNoteOffSerial (70) == 0, "el NoteOn atrasado no cuenta y su NoteOff no sale");
        CHECK (a.getNumSoundingNotes() == 3 && b.getNumSoundingNotes() == 1, "cuenta de notas sonando por instancia");
        CHECK (std::abs (a.getLastNoteOnSentMs (53) - t0 - 90.0) < 6.0, "hora del último envío de cada nota");

        // A se reinicia (bypass / prepareToPlay): sus 3 NoteOff, lo pendiente se tira; B sigue
        a.push ({ t0 + 5000.0, 0x90, 65, 100 });
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        a.requestReleaseAll();
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        lines = readPortFile (file);
        std::set<int> offs;
        bool any65 = false, offsOnly = true;
        for (size_t i = 4; i < lines.size(); ++i)
        {
            any65 = any65 || lines[i].d1 == 65;
            offsOnly = offsOnly && (lines[i].status & 0xF0) == 0x80;
            offs.insert (lines[i].d1);
        }
        CHECK ((offs == std::set<int> { 53, 57, 60 }) && offsOnly && ! any65,
               "releaseAll: los NoteOff de A (F3 A3 C4) y nada de lo pendiente");
        CHECK (a.getNumSoundingNotes() == 0 && b.getNumSoundingNotes() == 1 && b.getNoteOffSerial (57) == 1,
               "B sigue sonando y también sabe que salieron esos NoteOff");

        // B se desconecta (instancia cerrada): su nota se apaga
        const size_t before = lines.size();
        port.removeFeed (b);
        lines = readPortFile (file);
        CHECK (lines.size() == before + 1 && lines.back().d1 == 48 && (lines.back().status & 0xF0) == 0x80,
               "al cerrar una instancia, su nota recibe el NoteOff");

        // Dos instancias tocan la misma nota: sale UNA vez y se apaga con la última
        {
            ChordPortFeed c, d;
            port.addFeed (c);
            port.addFeed (d);
            const size_t start = readPortFile (file).size();
            const double t1 = juce::Time::getMillisecondCounterHiRes();
            c.push ({ t1, 0x90, 72, 100 });
            d.push ({ t1, 0x90, 72, 100 });
            std::this_thread::sleep_for (std::chrono::milliseconds (30));
            const size_t afterOn = readPortFile (file).size();
            d.push ({ t1 + 30.0, 0x80, 72, 0 });
            std::this_thread::sleep_for (std::chrono::milliseconds (30));
            const size_t afterFirstOff = readPortFile (file).size();
            c.push ({ t1 + 60.0, 0x80, 72, 0 });
            std::this_thread::sleep_for (std::chrono::milliseconds (30));
            auto all = readPortFile (file);
            CHECK (afterOn == start + 1 && afterFirstOff == start + 1, "la misma nota de dos instancias sale una sola vez y sigue sonando mientras una la tenga");
            CHECK (all.size() == start + 2 && (all.back().status & 0xF0) == 0x80 && all.back().d1 == 72,
                   "se apaga cuando la suelta la última instancia");
            port.removeFeed (c);
            port.removeFeed (d);
        }

        // FIFO lleno (nadie lo vacía): push descarta sin bloquear ni pedir memoria
        int accepted = 0;
        {
            AllocationGuard guard;
            for (int i = 0; i < 5000; ++i)
                accepted += unconnected.push ({ t0, 0x90, 60, 100 }) ? 1 : 0;
            CHECK (guard.count() == 0, "push() (hilo de audio) no pide memoria");
        }
        CHECK (accepted == ChordPortFeed::kCapacity - 1, "FIFO lleno: descarta lo que no cabe");

        port.removeFeed (a);
    }
    CHECK (readPortFile (file).size() == 10, "en total: 5 NoteOn y sus 5 NoteOff; nada más sale después de cerrar");
    setEnv ("SMARTKEYS_TEST_CHORD_PORT", nullptr);
    file.deleteFile();
   #endif
}

static void testRealtimeAllocations()
{
    std::printf ("[10] Camino de audio sin asignaciones de memoria\n");
    const double sr = 48000.0;
    const int block = 256;

    ScaleChordEngine engine;
    engine.setState (5, 0, ScaleChordEngine::getPitchClassMask (5, 0));
    VoicePitchTracker tracker;
    tracker.prepare (sr);
    FlKeysPianoSynth synth;
    synth.prepare (sr, block);
    MidiOutputRouter router;
    StrumScheduler strum;
    strum.prepare (sr);
    strum.setStrum (40.0, false);
    juce::MidiBuffer routed, out;
    routed.ensureSize (16384);
    out.ensureSize (16384);
    juce::AudioBuffer<float> audio (2, block);
    std::vector<float> mic ((size_t) block);

    auto runBlock = [&] (int b)
    {
        routed.clear();
        out.clear();
        for (int i = 0; i < block; ++i)
            mic[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (b * block + i) / sr);
        const float* chans[1] = { mic.data() };
        const int ne = tracker.process (chans, 1, block, engine);
        for (int k = 0; k < ne; ++k)
        {
            const auto& ev = tracker.getEvent (k);
            if (ev.isNoteOn) router.handleNoteOn (MidiOutputRouter::Source::Voice, ev.note, 1, (juce::uint8) ev.velocity, ev.sampleOffset, engine, routed);
            else             router.handleNoteOff (MidiOutputRouter::Source::Voice, ev.note, ev.sampleOffset, routed);
        }
        if (b % 7 == 0) router.handleNoteOn (MidiOutputRouter::Source::Midi, 60 + (b % 12), 1, 100, 3, engine, routed);
        if (b % 7 == 3) router.handleNoteOff (MidiOutputRouter::Source::Midi, 60 + ((b - 3) % 12), 9, routed);
        if (b % 11 == 0) router.handleNoteOn (MidiOutputRouter::Source::Screen, 48 + (b % 5), 1, 90, 5, engine, routed);
        if (b % 11 == 5) router.handleNoteOff (MidiOutputRouter::Source::Screen, 48 + ((b - 5) % 5), 7, routed);
        if (b % 97 == 50) router.releaseAll (11, routed);   // CC 120/123 del DAW
        strum.process (routed, out, block);
        if (b % 50 == 0) engine.setState ((b / 50) % 12, (b / 50) % kNumScales, ScaleChordEngine::getPitchClassMask ((b / 50) % 12, (b / 50) % kNumScales));
        if (b % 90 == 0) engine.setMode ((OperationMode) ((b / 90) % 3));                 // Smart / Single / Complex
        if (b % 130 == 0) engine.setComplexVariation ((ComplexVariation) ((b / 130) % 3));
        if (b % 37 == 0)
        {
            // Ráfaga de 40 notas con 32 voces: obliga a robar voces (sin memoria dinámica)
            for (int n = 0; n < 40; ++n)
                out.addEvent (juce::MidiMessage::noteOn (1, 36 + n, (juce::uint8) 90), n);
        }
        if (b % 37 == 20)
            for (int n = 0; n < 40; ++n)
                out.addEvent (juce::MidiMessage::noteOff (1, 36 + n), n);
        audio.clear();
        synth.render (audio, out, 0, block);
    };

    for (int b = 0; b < 64; ++b) runBlock (b);   // calentamiento

    long allocs = 0;
    {
        AllocationGuard guard;
        for (int b = 64; b < 2000; ++b) runBlock (b);
        allocs = guard.count();
    }
    std::printf ("    1936 bloques (tracker + router + STRUM + motor + sinte con robo de voces): %ld asignaciones\n", allocs);
    CHECK (allocs == 0, "cero asignaciones en el hilo de audio");
}

//==============================================================================
int main()
{
    std::printf ("SmartKeys — pruebas del motor\n\n");

    testScaleDatabase();
    testQuantization();
    testSmartChords();
    testAllScalesExhaustive();
    testCustomMask();
    testMicrotuning();
    testRouter();
    testPitchTracker();
    testSynth();
    testChordNamer();
    testComplexChords();
    testComplexExhaustive();
    testComplexRouterAndVoice();
    testComplexPolyphonyAndLevel();
    testStrumScheduler();
    testRealtimeAllocations();
    testMicRingBuffer();
    testSystemMicController();
    testChordPort();

    std::printf ("\n%d comprobaciones, %d fallos\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
