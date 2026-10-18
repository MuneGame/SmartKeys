/*
  ==============================================================================

    ChordRecorder.h
    SmartKeys — Capturador MIDI en vivo + exportación para "DRAG CHORDS TO
    PIANO ROLL".

    Por qué existe: FL Studio y Ableton Live graban en la pista lo que TÚ tocas
    (la tecla disparadora), no lo que el plugin emite por su salida MIDI. Este
    grabador registra en tiempo real TODAS las notas de salida (los acordes
    completos de SMART / COMPLEX CHORDS, también los disparados por la voz) y
    las convierte en un archivo .mid que se arrastra al Piano Roll de cualquier
    DAW o plugin con todas las notas ya pintadas.

    - Hilo de audio: publica cada NoteOn/NoteOff de salida en un FIFO lock-free
      con su posición musical (negras) según el tempo del host.
    - Hilo de mensajes: vacía el FIFO, agrupa acordes y genera el MIDI
      (tempo del proyecto, 4/4, cuantización opcional a 1/16).

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace smartkeys
{

//==============================================================================
struct CapturedNoteEvent
{
    double timeQn = 0.0;    // posición en negras del reloj musical del plugin
    int note = 0;
    int velocity = 0;
    bool isNoteOn = false;
};

//==============================================================================
// FIFO lock-free: 1 productor (hilo de audio) y 1 consumidor (hilo de mensajes)
class CaptureFifo
{
public:
    static constexpr int kCapacity = 4096;

    bool push (const CapturedNoteEvent& e) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) { buffer[(size_t) scope.startIndex1] = e; return true; }
        if (scope.blockSize2 > 0) { buffer[(size_t) scope.startIndex2] = e; return true; }
        return false;   // lleno: se descarta (nunca bloquea el audio)
    }

    template <typename Fn>
    int drain (Fn&& fn)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) fn (buffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) fn (buffer[(size_t) (scope.startIndex2 + i)]);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<CapturedNoteEvent, (size_t) kCapacity> buffer {};
};

//==============================================================================
class ChordRecorder
{
public:
    static constexpr int    kMaxEvents          = 20000;
    static constexpr double kChordGroupWindowQn = 0.03;   // notas casi simultáneas = mismo acorde
    static constexpr int    kTicksPerQuarter    = 960;

    struct NoteSpan
    {
        int note = 0;
        int velocity = 0;
        double start = 0.0;
        double end = 0.0;
    };

    //==========================================================================
    void add (const CapturedNoteEvent& e)
    {
        if ((int) events.size() >= kMaxEvents)
        {
            full = true;
            return;
        }

        if (e.isNoteOn)
        {
            if (numChords == 0 || std::abs (e.timeQn - lastChordOnTime) > kChordGroupWindowQn)
                ++numChords;
            lastChordOnTime = e.timeQn;
        }

        events.push_back (e);
    }

    void clear()
    {
        events.clear();
        numChords = 0;
        full = false;
        lastChordOnTime = -1.0e9;
    }

    int  getNumChords() const noexcept   { return numChords; }
    int  getNumEvents() const noexcept   { return (int) events.size(); }
    bool isEmpty() const noexcept        { return numChords == 0; }
    bool isFull() const noexcept         { return full; }

    //==========================================================================
    // Empareja NoteOn/NoteOff por nota (FIFO). Las notas aún sostenidas se
    // cierran en 'endTime'.
    std::vector<NoteSpan> buildSpans (double endTime) const
    {
        std::vector<NoteSpan> spans;
        std::array<std::vector<size_t>, 128> open;

        for (const auto& e : events)
        {
            const int n = juce::jlimit (0, 127, e.note);
            if (e.isNoteOn)
            {
                open[(size_t) n].push_back (spans.size());
                spans.push_back ({ n, juce::jlimit (1, 127, e.velocity), e.timeQn, -1.0 });
            }
            else if (! open[(size_t) n].empty())
            {
                spans[open[(size_t) n].front()].end = e.timeQn;
                open[(size_t) n].erase (open[(size_t) n].begin());
            }
        }

        for (auto& s : spans)
            if (s.end < s.start)
                s.end = juce::jmax (endTime, s.start);

        return spans;
    }

    // Spans listos para el MIDI: cuantizados (opcional), sin solapes de la
    // misma nota y desplazados para que el primer acorde empiece en 0.
    std::vector<NoteSpan> buildExportSpans (bool quantize, double gridQn = 0.25) const
    {
        const double lastTime = events.empty() ? 0.0 : events.back().timeQn;
        auto spans = buildSpans (lastTime + 1.0);
        if (spans.empty())
            return spans;

        const double minLength = quantize ? gridQn : 1.0 / 64.0;
        for (auto& s : spans)
        {
            if (quantize)
            {
                s.start = std::round (s.start / gridQn) * gridQn;
                s.end   = std::round (s.end / gridQn) * gridQn;
            }
            s.end = juce::jmax (s.end, s.start + minLength);
        }

        // Una nota repetida no puede solaparse consigo misma
        std::sort (spans.begin(), spans.end(), [] (const NoteSpan& a, const NoteSpan& b)
        {
            return a.note != b.note ? a.note < b.note : a.start < b.start;
        });
        for (size_t i = 0; i + 1 < spans.size(); ++i)
            if (spans[i].note == spans[i + 1].note && spans[i].end > spans[i + 1].start)
                spans[i].end = spans[i + 1].start;
        spans.erase (std::remove_if (spans.begin(), spans.end(),
                                     [] (const NoteSpan& s) { return s.end <= s.start + 1.0e-9; }),
                     spans.end());

        std::sort (spans.begin(), spans.end(), [] (const NoteSpan& a, const NoteSpan& b)
        {
            if (std::abs (a.start - b.start) > 1.0e-12)
                return a.start < b.start;
            return a.note < b.note;
        });

        if (! spans.empty())
        {
            double first = spans.front().start;
            if (quantize)
                first = std::floor (first / gridQn + 1.0e-9) * gridQn;
            for (auto& s : spans)
            {
                s.start -= first;
                s.end   -= first;
            }
        }
        return spans;
    }

    //==========================================================================
    // MIDI de 1 pista: nombre, tempo del proyecto, compás 4/4 y las notas (canal 1)
    juce::MidiFile createMidiFile (double bpm, bool quantize) const
    {
        const auto spans = buildExportSpans (quantize);

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "SmartKeys Chords"), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

        for (const auto& s : spans)
        {
            const double on  = s.start * kTicksPerQuarter;
            const double off = s.end   * kTicksPerQuarter;
            seq.addEvent (juce::MidiMessage::noteOn (1, s.note, (juce::uint8) s.velocity), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, s.note, (juce::uint8) 0), off);
        }

        seq.sort();
        seq.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);
        file.addTrack (seq);
        return file;
    }

    // Escribe el .mid en Música/SmartKeys (o en la carpeta temporal si no se
    // puede) y devuelve el archivo; File() si no hay nada grabado.
    juce::File writeMidiFile (const juce::String& baseName, double bpm, bool quantize) const
    {
        if (isEmpty())
            return {};

        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SmartKeys");
        if (! dir.createDirectory().wasOk() || ! dir.hasWriteAccess())
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SmartKeys");
            dir.createDirectory();
        }

        auto file = dir.getNonexistentChildFile (juce::File::createLegalFileName (baseName), ".mid", false);
        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
            return {};

        createMidiFile (bpm, quantize).writeTo (stream, 1);
        stream.flush();
        return file;
    }

private:
    std::vector<CapturedNoteEvent> events;
    int numChords = 0;
    bool full = false;
    double lastChordOnTime = -1.0e9;
};

} // namespace smartkeys
