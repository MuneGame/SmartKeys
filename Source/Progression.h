/*
  ==============================================================================

    Progression.h
    SmartKeys — Carril de progresión (estilo Klimper) + captura inteligente +
    exportación "DRAG CHORDS TO PIANO ROLL".

    1. CAPTURA INTELIGENTE (hilo de audio -> CaptureFifo -> hilo de mensajes)
       Sólo se capturan acordes cuando:
         - el DAW reproduce o graba (o sólo graba, según el ajuste), con la
           posición exacta del compás (ppqPosition) y el tempo del proyecto;
         - o el botón REC del plugin está armado (con el DAW detenido usa un
           reloj propio al tempo del proyecto).
       Con el DAW detenido, probar teclas NO guarda nada.
       Cada acorde se guarda COMPLETO (todas sus notas apiladas), con su
       posición, su duración y el retardo de entrada de cada nota (STRUM).
       Tomas: "Reemplazar" (la toma nueva sustituye a la progresión al tocar
       el primer acorde) o "Añadir" (se conserva lo anterior; lo que se vuelve
       a tocar encima se sobrescribe, como un punch-in). Con el DAW en bucle,
       cada vuelta reemplaza a la anterior: nunca se duplican acordes.

    2. CARRIL DE PROGRESIÓN: bloques de acordes por compases, construidos a
       mano desde la paleta (clic o arrastrar) o grabados. Se pueden mover,
       alargar, duplicar, borrar y deshacer.

    3. EXPORTACIÓN: MIDI de 960 PPQ con el tempo y el compás del proyecto,
       todas las notas de cada acorde apiladas en su tiempo exacto (desde el
       compás 1), cuantización opcional (none, 1/32, 1/16, 1/8, 1/4, tresillos)
       y el mismo desfase de STRUM que sonó.

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include "ScaleChordEngine.h"
#include "StrumScheduler.h"

namespace smartkeys
{

//==============================================================================
// Rejilla de cuantización (índice guardado en el estado del plugin)
namespace QuantizeGrid
{
    inline juce::StringArray getNames()
    {
        return { "none", "1/32", "1/16", "1/8", "1/4", "1/8 triplets", "1/16 triplets" };
    }

    inline double getGridPpq (int index) noexcept
    {
        switch (index)
        {
            case 1:  return 0.125;
            case 2:  return 0.25;
            case 3:  return 0.5;
            case 4:  return 1.0;
            case 5:  return 1.0 / 3.0;
            case 6:  return 1.0 / 6.0;
            default: return 0.0;
        }
    }

    static constexpr int kDefault = 2;   // 1/16
}

// Duración de los acordes añadidos desde la paleta (en negras)
namespace NewChordLength
{
    inline juce::StringArray getNames()
    {
        return { juce::String::fromUTF8 ("1 tiempo"), juce::String::fromUTF8 ("2 tiempos"),
                 juce::String::fromUTF8 ("1 compás"), juce::String::fromUTF8 ("2 compases") };
    }

    inline double getBeats (int index, double beatsPerBar) noexcept
    {
        switch (index)
        {
            case 0:  return 1.0;
            case 1:  return 2.0;
            case 3:  return 2.0 * beatsPerBar;
            default: return beatsPerBar;
        }
    }

    static constexpr int kDefault = 2;   // 1 compás
}

//==============================================================================
// Evento del capturador (hilo de audio -> hilo de mensajes)
struct CaptureEvent
{
    enum Type : uint8_t { TakeStart = 0, PassStart, Region, ChordOn, ChordOff, CloseAll, TakeEnd };
    static constexpr int kMaxNotes = 8;

    uint8_t type = Region;
    bool replace = false;          // TakeStart: la toma nueva reemplaza la progresión
    bool freeClock = false;        // TakeStart: reloj propio (REC con el DAW detenido)
    bool strumDescending = false;  // ChordOn
    uint8_t count = 0;             // ChordOn: número de notas
    int32_t passId = 0;
    uint32_t chordId = 0;
    double ppq = 0.0;              // posición en negras
    double ppqEnd = 0.0;           // Region: final del tramo recorrido
    float strumMs = 0.0f;          // ChordOn: perilla STRUM al sonar el acorde
    float bpm = 120.0f;            // ChordOn: tempo (para convertir ms -> negras)
    std::array<int8_t, kMaxNotes> notes {};
    std::array<uint8_t, kMaxNotes> velocities {};
};

// FIFO lock-free: 1 productor (hilo de audio) y 1 consumidor (hilo de mensajes)
class CaptureFifo
{
public:
    static constexpr int kCapacity = 8192;

    bool push (const CaptureEvent& e) noexcept
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
    std::array<CaptureEvent, (size_t) kCapacity> buffer {};
};

//==============================================================================
// Un acorde del carril de progresión
struct ChordBlock
{
    static constexpr int kMaxNotes = ScaleChordEngine::kMaxChordNotes;

    double start = 0.0;                         // negras desde el compás 1
    double length = 4.0;                        // negras
    int count = 0;
    std::array<int, kMaxNotes> notes {};        // de grave a agudo
    std::array<int, kMaxNotes> velocities {};
    std::array<double, kMaxNotes> offsets {};   // retardo de entrada de cada nota (negras); sólo grabados
    bool recorded = false;                      // false = construido desde la paleta
    uint32_t uid = 0;                           // identificador estable (selección en el carril)

    // Estado de grabación
    bool open = false;                          // la tecla sigue pulsada
    uint32_t chordId = 0;
    int passId = -1;

    double end() const noexcept { return start + length; }

    std::vector<int> getNotes() const
    {
        return std::vector<int> (notes.begin(), notes.begin() + count);
    }
};

struct ExportNote
{
    int note = 60;
    int velocity = 100;
    double start = 0.0;
    double end = 1.0;
};

// Retardos de STRUM (negras) para las notas de un acorde, ordenadas ASC o DESC
inline void computeStrumOffsets (const ChordBlock& b, double strumMs, bool descending, double bpm,
                                 std::array<double, ChordBlock::kMaxNotes>& offsets) noexcept
{
    offsets.fill (0.0);

    // Las notas ya están de grave a agudo: ASC entra en ese orden, DESC al revés
    const double msToPpq = juce::jmax (1.0, bpm) / 60000.0;
    for (int rank = 0; rank < b.count; ++rank)
    {
        const int idx = descending ? b.count - 1 - rank : rank;
        offsets[(size_t) idx] = strumOffsetMs (rank, strumMs) * msToPpq;
    }
}

//==============================================================================
// Instantánea de la progresión para la vista previa (hilo de audio): POD fijo
struct PreviewSnapshot
{
    static constexpr int kMaxChords = 512;

    struct Chord
    {
        double start = 0.0, end = 0.0;
        int count = 0;
        std::array<int, ChordBlock::kMaxNotes> notes {};
        std::array<juce::uint8, ChordBlock::kMaxNotes> velocities {};
    };

    int numChords = 0;
    double loopLength = 4.0;
    std::array<Chord, (size_t) kMaxChords> chords {};
};

//==============================================================================
class ProgressionModel
{
public:
    static constexpr int kMaxBlocks = PreviewSnapshot::kMaxChords;
    static constexpr int kMaxUndo   = 40;
    static constexpr int kTicksPerQuarter = 960;
    static constexpr double kMinLength = 1.0 / 32.0;

    //==========================================================================
    const std::vector<ChordBlock>& getBlocks() const noexcept   { return blocks; }
    int size() const noexcept                                    { return (int) blocks.size(); }
    bool isEmpty() const noexcept                                { return blocks.empty(); }
    bool isFull() const noexcept                                 { return (int) blocks.size() >= kMaxBlocks; }
    uint32_t getVersion() const noexcept                         { return version; }
    bool isTakeOpen() const noexcept                             { return takeOpen; }

    double getContentEnd() const noexcept
    {
        double e = 0.0;
        for (const auto& b : blocks)
            e = juce::jmax (e, b.end());
        return e;
    }

    static double nextBarAfter (double t, double beatsPerBar) noexcept
    {
        if (t <= 1.0e-9)
            return 0.0;
        return std::ceil (t / beatsPerBar - 1.0e-9) * beatsPerBar;
    }

    //==========================================================================
    // Deshacer
    bool canUndo() const noexcept { return ! undoStack.empty(); }

    void pushUndo()
    {
        undoStack.push_back (blocks);
        if ((int) undoStack.size() > kMaxUndo)
            undoStack.erase (undoStack.begin());
    }

    void undo()
    {
        if (undoStack.empty())
            return;
        blocks = std::move (undoStack.back());
        undoStack.pop_back();
        for (auto& b : blocks)
            b.open = false;
        changed();
    }

    //==========================================================================
    // Edición (desde la paleta / el carril)
    static ChordBlock makeBlock (const int* notes, const int* velocities, int count, double start, double length)
    {
        ChordBlock b;
        b.start = juce::jmax (0.0, start);
        b.length = juce::jmax (kMinLength, length);
        b.count = 0;
        for (int i = 0; i < count && b.count < ChordBlock::kMaxNotes; ++i)
        {
            if (notes[i] < 0 || notes[i] > 127)
                continue;
            b.notes[(size_t) b.count] = notes[i];
            b.velocities[(size_t) b.count] = juce::jlimit (1, 127, velocities != nullptr ? velocities[i] : 100);
            ++b.count;
        }
        sortNotes (b);
        return b;
    }

    // Añade el acorde a continuación del último; devuelve su índice
    int append (const ChordBlock& block)
    {
        if (isFull() || block.count == 0)
            return -1;
        pushUndo();
        ChordBlock b = block;
        b.start = getContentEnd();
        b.uid = nextUid++;
        blocks.push_back (b);
        return indexAfterInsert (b.uid);
    }

    // Inserta en 'time' (ajustado a la rejilla); los acordes posteriores se desplazan
    int insertAt (const ChordBlock& block, double time, double grid)
    {
        if (isFull() || block.count == 0)
            return -1;
        pushUndo();

        double t = juce::jmax (0.0, grid > 0.0 ? snap (time, grid) : time);
        for (const auto& b : blocks)
        {
            if (t > b.start + 1.0e-9 && t < b.end() - 1.0e-9)
            {
                t = (time - b.start) < b.length * 0.5 ? b.start : b.end();
                break;
            }
        }

        // Si hay acordes a partir de 't', se corren para hacer sitio
        bool occupied = false;
        for (const auto& b : blocks)
            occupied = occupied || (b.start >= t - 1.0e-9 && b.start < t + block.length - 1.0e-9);
        if (occupied)
            for (auto& b : blocks)
                if (b.start >= t - 1.0e-9)
                    b.start += block.length;

        ChordBlock b = block;
        b.start = t;
        b.uid = nextUid++;
        blocks.push_back (b);
        return indexAfterInsert (b.uid);
    }

    void moveBlock (int index, double newStart)
    {
        if (! juce::isPositiveAndBelow (index, size()))
            return;
        blocks[(size_t) index].start = juce::jmax (0.0, newStart);
        sortBlocks();
        changed();
    }

    void resizeBlock (int index, double newLength)
    {
        if (! juce::isPositiveAndBelow (index, size()))
            return;
        auto& b = blocks[(size_t) index];
        b.length = juce::jmax (kMinLength, newLength);
        dropNotesBeyondEnd (b);
        changed();
    }

    void removeBlock (int index)
    {
        if (! juce::isPositiveAndBelow (index, size()))
            return;
        pushUndo();
        blocks.erase (blocks.begin() + index);
        changed();
    }

    int duplicateBlock (int index)
    {
        if (! juce::isPositiveAndBelow (index, size()) || isFull())
            return -1;
        ChordBlock copy = blocks[(size_t) index];
        copy.open = false;
        copy.chordId = 0;
        copy.passId = -1;
        return insertAt (copy, blocks[(size_t) index].end(), 0.0);
    }

    void clear()
    {
        if (blocks.empty())
            return;
        pushUndo();
        blocks.clear();
        changed();
    }

    int indexOfUid (uint32_t uid) const noexcept
    {
        for (int i = 0; i < size(); ++i)
            if (blocks[(size_t) i].uid == uid)
                return i;
        return -1;
    }

    int findBlockIndex (uint32_t chordId) const noexcept
    {
        for (int i = 0; i < size(); ++i)
            if (blocks[(size_t) i].open && blocks[(size_t) i].chordId == chordId)
                return i;
        return -1;
    }

    //==========================================================================
    // Captura (eventos del hilo de audio)
    void applyCaptureEvent (const CaptureEvent& e, double beatsPerBar)
    {
        switch (e.type)
        {
            case CaptureEvent::TakeStart:
                closeOpenBlocks (mapTime (lastRegionEndRaw));
                takeOpen = true;
                takeReplace = e.replace;
                takeFree = e.freeClock;
                takeHasChords = false;
                freeOffset = 0.0;
                currentPass = e.passId;
                lastRegionEndRaw = e.ppq;
                break;

            case CaptureEvent::PassStart:
                closeOpenBlocks (mapTime (lastRegionEndRaw));
                currentPass = e.passId;
                lastRegionEndRaw = e.ppq;
                break;

            case CaptureEvent::Region:
                if (! takeOpen)
                    break;
                lastRegionEndRaw = e.ppqEnd;
                if (takeHasChords && ! takeFree)
                    overwrite (mapTime (e.ppq), mapTime (e.ppqEnd));
                growOpenBlocks (mapTime (e.ppqEnd));
                break;

            case CaptureEvent::ChordOn:
            {
                if (! takeOpen || e.count == 0)
                    break;

                if (! takeHasChords)
                {
                    // Primer acorde de la toma: aquí (y no al pulsar PLAY) se reemplaza lo
                    // anterior, así reproducir el proyecto sin tocar nada nunca borra nada
                    takeHasChords = true;
                    pushUndo();
                    const double contentEnd = getContentEnd();
                    if (takeReplace)
                        blocks.clear();
                    if (takeFree)
                        freeOffset = (takeReplace ? 0.0 : nextBarAfter (contentEnd, beatsPerBar)) - e.ppq;
                }

                if (isFull())
                    break;

                ChordBlock b;
                b.uid = nextUid++;
                b.start = juce::jmax (0.0, mapTime (e.ppq));
                b.length = 0.0;
                b.recorded = true;
                b.open = true;
                b.chordId = e.chordId;
                b.passId = currentPass;
                b.count = 0;
                for (int i = 0; i < juce::jmin ((int) e.count, ChordBlock::kMaxNotes); ++i)
                {
                    b.notes[(size_t) b.count] = (int) e.notes[(size_t) i];
                    b.velocities[(size_t) b.count] = juce::jlimit (1, 127, (int) e.velocities[(size_t) i]);
                    ++b.count;
                }
                sortNotes (b);
                computeStrumOffsets (b, (double) e.strumMs, e.strumDescending, (double) e.bpm, b.offsets);
                blocks.push_back (b);
                sortBlocks();
                changed();
                break;
            }

            case CaptureEvent::ChordOff:
            {
                const int index = findBlockIndex (e.chordId);
                if (index >= 0)
                {
                    closeBlock (blocks[(size_t) index], mapTime (e.ppq));
                    changed();
                }
                break;
            }

            case CaptureEvent::CloseAll:
                closeOpenBlocks (mapTime (e.ppq));
                break;

            case CaptureEvent::TakeEnd:
                closeOpenBlocks (mapTime (e.ppq));
                takeOpen = false;
                break;

            default:
                break;
        }
    }

    // Cabezal de la toma en curso (para dibujarlo en el carril)
    double getCaptureHead() const noexcept { return mapTime (lastRegionEndRaw); }

    //==========================================================================
    // Exportación: todas las notas de cada acorde apiladas en su tiempo exacto
    std::vector<ExportNote> buildExportNotes (double gridPpq, double strumMs, bool strumDescending, double bpm) const
    {
        std::vector<ExportNote> out;
        out.reserve ((size_t) size() * ChordBlock::kMaxNotes);

        for (const auto& b : blocks)
        {
            double s = b.start;
            double e = b.start + juce::jmax (kMinLength, b.length);
            if (gridPpq > 0.0)
            {
                s = std::round (s / gridPpq) * gridPpq;
                e = std::round (e / gridPpq) * gridPpq;
                if (e < s + gridPpq * 0.5)
                    e = s + gridPpq;
            }

            std::array<double, ChordBlock::kMaxNotes> offs {};
            if (b.recorded)
                offs = b.offsets;
            else
                computeStrumOffsets (b, strumMs, strumDescending, bpm, offs);

            for (int i = 0; i < b.count; ++i)
            {
                const double ns = s + offs[(size_t) i];
                if (ns >= e - 1.0e-6)
                    continue;   // con un STRUM más largo que el acorde, esa nota no llega a sonar
                out.push_back ({ b.notes[(size_t) i], b.velocities[(size_t) i], ns, e });
            }
        }

        // Una misma nota no puede solaparse consigo misma (acordes consecutivos que la comparten)
        std::sort (out.begin(), out.end(), [] (const ExportNote& a, const ExportNote& b)
        {
            if (a.note != b.note) return a.note < b.note;
            return a.start < b.start;
        });
        for (size_t i = 0; i + 1 < out.size(); ++i)
            if (out[i].note == out[i + 1].note && out[i].end > out[i + 1].start)
                out[i].end = out[i + 1].start;
        out.erase (std::remove_if (out.begin(), out.end(),
                                   [] (const ExportNote& n) { return n.end <= n.start + 1.0e-9; }),
                   out.end());

        std::sort (out.begin(), out.end(), [] (const ExportNote& a, const ExportNote& b)
        {
            if (std::abs (a.start - b.start) > 1.0e-12) return a.start < b.start;
            return a.note < b.note;
        });
        return out;
    }

    juce::MidiFile createMidiFile (double bpm, double gridPpq, double strumMs, bool strumDescending,
                                   int numerator, int denominator) const
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "SmartKeys Chords"), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::jlimit (1, 32, numerator),
                                                                 juce::jlimit (1, 32, denominator)), 0.0);

        for (const auto& n : buildExportNotes (gridPpq, strumMs, strumDescending, bpm))
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity), n.start * kTicksPerQuarter);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.note, (juce::uint8) 0), n.end * kTicksPerQuarter);
        }

        seq.sort();
        seq.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);
        file.addTrack (seq);
        return file;
    }

    // Escribe el .mid en Música/SmartKeys (o en la carpeta temporal) y lo devuelve
    juce::File writeMidiFile (const juce::String& baseName, double bpm, double gridPpq, double strumMs,
                              bool strumDescending, int numerator, int denominator) const
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

        createMidiFile (bpm, gridPpq, strumMs, strumDescending, numerator, denominator).writeTo (stream, 1);
        stream.flush();
        return file;
    }

    //==========================================================================
    // Vista previa
    void fillSnapshot (PreviewSnapshot& s, double beatsPerBar) const noexcept
    {
        s.numChords = 0;
        for (const auto& b : blocks)
        {
            if (s.numChords >= PreviewSnapshot::kMaxChords)
                break;
            auto& c = s.chords[(size_t) s.numChords++];
            c.start = b.start;
            c.end = b.start + juce::jmax (kMinLength, b.length);
            c.count = b.count;
            for (int i = 0; i < b.count; ++i)
            {
                c.notes[(size_t) i] = b.notes[(size_t) i];
                c.velocities[(size_t) i] = (juce::uint8) juce::jlimit (1, 127, b.velocities[(size_t) i]);
            }
        }
        s.loopLength = juce::jmax (beatsPerBar, nextBarAfter (getContentEnd(), beatsPerBar));
    }

    //==========================================================================
    // Estado del proyecto
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Progression");
        xml->setAttribute ("version", 1);
        for (const auto& b : blocks)
        {
            auto* c = xml->createNewChildElement ("Chord");
            c->setAttribute ("start", b.start);
            c->setAttribute ("length", juce::jmax (kMinLength, b.length));
            c->setAttribute ("recorded", b.recorded ? 1 : 0);
            juce::String notes, vels, offs;
            for (int i = 0; i < b.count; ++i)
            {
                notes << (i > 0 ? " " : "") << b.notes[(size_t) i];
                vels  << (i > 0 ? " " : "") << b.velocities[(size_t) i];
                offs  << (i > 0 ? " " : "") << juce::String (b.offsets[(size_t) i], 6);
            }
            c->setAttribute ("notes", notes);
            c->setAttribute ("vels", vels);
            c->setAttribute ("offsets", offs);
        }
        return xml;
    }

    void fromXml (const juce::XmlElement& xml)
    {
        std::vector<ChordBlock> loaded;
        for (auto* c : xml.getChildWithTagNameIterator ("Chord"))
        {
            if ((int) loaded.size() >= kMaxBlocks)
                break;

            juce::StringArray notes, vels, offs;
            notes.addTokens (c->getStringAttribute ("notes"), " ", "");
            vels.addTokens (c->getStringAttribute ("vels"), " ", "");
            offs.addTokens (c->getStringAttribute ("offsets"), " ", "");
            notes.removeEmptyStrings();

            ChordBlock b;
            b.start = juce::jmax (0.0, c->getDoubleAttribute ("start"));
            b.length = juce::jmax (kMinLength, c->getDoubleAttribute ("length", 4.0));
            b.recorded = c->getIntAttribute ("recorded") != 0;
            for (int i = 0; i < notes.size() && b.count < ChordBlock::kMaxNotes; ++i)
            {
                const int n = notes[i].getIntValue();
                if (n < 0 || n > 127)
                    continue;
                b.notes[(size_t) b.count] = n;
                b.velocities[(size_t) b.count] = juce::jlimit (1, 127, i < vels.size() ? vels[i].getIntValue() : 100);
                b.offsets[(size_t) b.count] = juce::jmax (0.0, i < offs.size() ? offs[i].getDoubleValue() : 0.0);
                ++b.count;
            }
            if (b.count > 0)
            {
                sortNotes (b);
                b.uid = nextUid++;
                loaded.push_back (b);
            }
        }

        blocks = std::move (loaded);
        sortBlocks();
        undoStack.clear();
        takeOpen = false;
        changed();
    }

    void changed() noexcept { ++version; }

private:
    //==========================================================================
    static double snap (double t, double grid) noexcept
    {
        return grid > 0.0 ? std::round (t / grid) * grid : t;
    }

    static void sortNotes (ChordBlock& b) noexcept
    {
        for (int i = 1; i < b.count; ++i)
        {
            const int n = b.notes[(size_t) i];
            const int v = b.velocities[(size_t) i];
            const double o = b.offsets[(size_t) i];
            int j = i - 1;
            while (j >= 0 && b.notes[(size_t) j] > n)
            {
                b.notes[(size_t) (j + 1)] = b.notes[(size_t) j];
                b.velocities[(size_t) (j + 1)] = b.velocities[(size_t) j];
                b.offsets[(size_t) (j + 1)] = b.offsets[(size_t) j];
                --j;
            }
            b.notes[(size_t) (j + 1)] = n;
            b.velocities[(size_t) (j + 1)] = v;
            b.offsets[(size_t) (j + 1)] = o;
        }
    }

    void sortBlocks()
    {
        std::stable_sort (blocks.begin(), blocks.end(),
                          [] (const ChordBlock& a, const ChordBlock& b) { return a.start < b.start; });
    }

    int indexAfterInsert (uint32_t uid)
    {
        sortBlocks();
        changed();
        return indexOfUid (uid);
    }

    // Quita las notas que, por su retardo de STRUM, no llegan a sonar
    static void dropNotesBeyondEnd (ChordBlock& b) noexcept
    {
        if (! b.recorded)
            return;
        int kept = 0;
        for (int i = 0; i < b.count; ++i)
        {
            if (b.offsets[(size_t) i] < b.length - 1.0e-9)   // la primera nota (retardo 0) siempre queda
            {
                b.notes[(size_t) kept] = b.notes[(size_t) i];
                b.velocities[(size_t) kept] = b.velocities[(size_t) i];
                b.offsets[(size_t) kept] = b.offsets[(size_t) i];
                ++kept;
            }
        }
        b.count = kept;
    }

    void closeBlock (ChordBlock& b, double t) noexcept
    {
        b.length = juce::jmax (kMinLength, t - b.start);
        b.open = false;
        dropNotesBeyondEnd (b);
    }

    void closeOpenBlocks (double t)
    {
        bool any = false;
        for (auto& b : blocks)
        {
            if (b.open)
            {
                closeBlock (b, juce::jmax (t, b.start + kMinLength));
                any = true;
            }
        }
        if (any)
            changed();
    }

    void growOpenBlocks (double t) noexcept
    {
        bool any = false;
        for (auto& b : blocks)
        {
            if (b.open && t - b.start > b.length)
            {
                b.length = t - b.start;
                any = true;
            }
        }
        if (any)
            changed();
    }

    // Lo que se vuelve a tocar encima se sobrescribe (bucles y punch-in)
    void overwrite (double a, double b)
    {
        bool any = false;
        for (size_t i = 0; i < blocks.size();)
        {
            auto& blk = blocks[i];
            if (blk.passId != currentPass)
            {
                if (blk.start >= a - 1.0e-9 && blk.start < b - 1.0e-9)
                {
                    blocks.erase (blocks.begin() + (std::ptrdiff_t) i);
                    any = true;
                    continue;
                }
                if (blk.start < a && blk.end() > a + 1.0e-9)
                {
                    blk.length = a - blk.start;
                    blk.open = false;
                    dropNotesBeyondEnd (blk);
                    any = true;
                }
            }
            ++i;
        }
        if (any)
            changed();
    }

    double mapTime (double raw) const noexcept { return raw + (takeFree ? freeOffset : 0.0); }

    //==========================================================================
    std::vector<ChordBlock> blocks;
    std::vector<std::vector<ChordBlock>> undoStack;
    uint32_t version = 1;
    uint32_t nextUid = 1;

    bool takeOpen = false, takeReplace = true, takeFree = false, takeHasChords = false;
    int currentPass = -1;
    double freeOffset = 0.0;
    double lastRegionEndRaw = 0.0;
};

//==============================================================================
// PALETA DE ACORDES (estilo Klimper): una columna por cada nota de la escala
// activa y una fila por tipo de acorde. Sólo se ofrecen acordes cuyas notas
// pertenecen a la escala (las casillas sin acorde diatónico quedan vacías).
enum class PaletteRow : int { Triad = 0, Seventh, Sixth, Add9, Sus2, Sus4 };
static constexpr int kNumPaletteRows = 6;

struct PaletteCell
{
    bool valid = false;
    int row = 0, column = 0;
    int rootPc = 0;
    int count = 0;
    std::array<int, ChordBlock::kMaxNotes> notes {};
    int quality = 0;   // 0 mayor, 1 menor, 2 disminuido, 3 aumentado, 4 suspendido
};

struct ChordPalette
{
    std::vector<int> columnRoots;      // clases de altura de la escala, desde la tónica
    std::vector<PaletteCell> cells;    // fila * numColumns + columna

    int getNumColumns() const noexcept { return (int) columnRoots.size(); }

    const PaletteCell& at (int row, int column) const noexcept
    {
        static const PaletteCell empty;
        const int index = row * getNumColumns() + column;
        return juce::isPositiveAndBelow (index, (int) cells.size()) ? cells[(size_t) index] : empty;
    }

    static juce::StringArray getRowNames()
    {
        return { juce::String::fromUTF8 ("TRÍADA"), juce::String::fromUTF8 ("7ª"), juce::String::fromUTF8 ("6ª"),
                 "ADD9", "SUS2", "SUS4" };
    }
};

inline ChordPalette buildChordPalette (int rootKey, uint16_t pitchClassMask)
{
    ChordPalette p;
    rootKey = ((rootKey % 12) + 12) % 12;
    if (pitchClassMask == 0)
        pitchClassMask = 0x0FFF;

    for (int i = 0; i < 12; ++i)
    {
        const int pc = (rootKey + i) % 12;
        if ((pitchClassMask >> pc) & 1u)
            p.columnRoots.push_back (pc);
    }

    const int numColumns = p.getNumColumns();
    p.cells.resize ((size_t) (numColumns * kNumPaletteRows));

    for (int column = 0; column < numColumns; ++column)
    {
        const int r = p.columnRoots[(size_t) column];
        auto in = [&] (int interval) { return ((pitchClassMask >> ((r + interval) % 12)) & 1u) != 0; };

        const int third = in (4) ? 4 : (in (3) ? 3 : -1);
        int fifth = -1;
        if (in (7))                        fifth = 7;
        else if (third == 3 && in (6))     fifth = 6;
        else if (third == 4 && in (8))     fifth = 8;

        int seventh = -1;
        if (in (11))                                      seventh = 11;
        else if (in (10))                                 seventh = 10;
        else if (third == 3 && fifth == 6 && in (9))      seventh = 9;

        int quality = 4;
        if (third == 4 && fifth == 7)      quality = 0;
        else if (third == 3 && fifth == 7) quality = 1;
        else if (third == 3 && fifth == 6) quality = 2;
        else if (third == 4 && fifth == 8) quality = 3;

        // Fundamental entre F3 y E4: los acordes quedan centrados alrededor del C4
        const int rootNote = 53 + ((r - 5 + 12) % 12);

        auto setCell = [&] (PaletteRow row, std::initializer_list<int> intervals, int q)
        {
            auto& c = p.cells[(size_t) ((int) row * numColumns + column)];
            c.valid = true;
            c.row = (int) row;
            c.column = column;
            c.rootPc = r;
            c.quality = q;
            c.count = 0;
            for (int iv : intervals)
                if (c.count < ChordBlock::kMaxNotes)
                    c.notes[(size_t) c.count++] = rootNote + iv;
        };

        const bool triad = third >= 0 && fifth >= 0;
        if (triad)
        {
            setCell (PaletteRow::Triad, { 0, third, fifth }, quality);
            if (seventh >= 0)
                setCell (PaletteRow::Seventh, { 0, third, fifth, seventh }, quality);
            if (fifth == 7 && in (9))
                setCell (PaletteRow::Sixth, { 0, third, 7, 9 }, quality);
            if (in (2))
                setCell (PaletteRow::Add9, { 0, third, fifth, 14 }, quality);
        }
        if (in (2) && in (7))
            setCell (PaletteRow::Sus2, { 0, 2, 7 }, 4);
        if (in (5) && in (7))
            setCell (PaletteRow::Sus4, { 0, 5, 7 }, 4);
    }

    return p;
}

} // namespace smartkeys
