/*
  ==============================================================================

    StrumScheduler.h
    SmartKeys — Entrada progresiva de las notas del acorde (STRUM / NOTE ENTRY)

    Recibe del router los NoteOn/NoteOff de los acordes (todas las notas de un
    acorde llegan en la misma muestra) y los reparte en el tiempo:

      - Las notas que llegan en la misma muestra forman un grupo; se ordenan de
        grave a agudo (ASC ▲) o de agudo a grave (DESC ▼).
      - La nota i del grupo (i = 0 la primera) entra con un retardo
            t_i = i · base · (1 + 0.15 · i)   [ms]
        así cada nota tarda un poco más que la anterior, como un pianista que
        desgrana el acorde. base = 0 -> todas entran juntas (bloque).
      - Cola de eventos por muestra (sample-accurate): los NoteOn pendientes se
        emiten en el bloque y la muestra exactos, aunque caigan varios bloques
        después. Capacidad fija, sin asignaciones ni bloqueos.
      - Si se suelta la tecla antes de que entren todas las notas, las
        pendientes se cancelan (nunca suenan) y las que ya sonaban reciben su
        NoteOff en ese mismo instante: cero notas colgadas y todas las notas
        terminan alineadas.
      - Lleva la cuenta de las notas que realmente suenan (para iluminar el
        teclado de la interfaz nota a nota).

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>
#include <cmath>

namespace smartkeys
{

// Retardo (ms) de la nota 'index' de un acorde con la perilla en 'baseMs'
inline double strumOffsetMs (int index, double baseMs) noexcept
{
    const double i = (double) index;
    return i * baseMs * (1.0 + 0.15 * i);
}

//==============================================================================
class StrumScheduler
{
public:
    static constexpr int    kMaxPending = 512;
    static constexpr int    kMaxGroup   = 128;
    static constexpr double kMaxStrumMs = 250.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        numPending = 0;
        sounding.fill (false);
        soundingChannel.fill (1);
        sampleClock = 0;
    }

    void setStrum (double baseMs, bool descending) noexcept
    {
        strumMs = juce::jlimit (0.0, kMaxStrumMs, baseMs);
        strumDescending = descending;
    }

    double getStrumMs() const noexcept      { return strumMs; }
    bool   isDescending() const noexcept    { return strumDescending; }
    int    getNumPending() const noexcept   { return numPending; }

    //==========================================================================
    // 'in'  : eventos del router de este bloque (ordenados por muestra)
    // 'out' : salida final (se añaden eventos; el llamador la vacía antes)
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples) noexcept
    {
        const int lastSample = juce::jmax (0, numSamples - 1);
        int groupPos = -1;
        groupSize = 0;

        for (const auto meta : in)
        {
            const int pos = juce::jlimit (0, lastSample, meta.samplePosition);

            if (pos != groupPos)
            {
                flushGroup (groupPos, out);
                emitDue (sampleClock + pos, out);   // pendientes que vencen antes de esta muestra
                groupPos = pos;
            }

            const auto msg = meta.getMessage();

            if (msg.isNoteOn())
            {
                if (groupSize < kMaxGroup)
                    group[(size_t) groupSize++] = { msg.getNoteNumber(), msg.getChannel(), msg.getVelocity() };
                else
                    emitNoteOn (msg.getNoteNumber(), msg.getChannel(), msg.getVelocity(), pos, out);
                continue;
            }

            // Un NoteOff u otro evento cierra el grupo de esta muestra
            flushGroup (pos, out);

            if (msg.isNoteOff())
            {
                handleNoteOff (msg.getNoteNumber(), msg.getChannel(), pos, out);
            }
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    cancelPendingOnChannel (msg.getChannel());
                out.addEvent (msg, pos);
            }
        }

        flushGroup (groupPos, out);
        emitDue (sampleClock + numSamples, out);
        sampleClock += numSamples;
    }

    // Notas que suenan ahora mismo (bitmask de 128 bits)
    void getSoundingBitmask (uint64_t& lo, uint64_t& hi) const noexcept
    {
        lo = hi = 0;
        for (int n = 0; n < 64; ++n)
            if (sounding[(size_t) n])
                lo |= (uint64_t) 1 << n;
        for (int n = 64; n < 128; ++n)
            if (sounding[(size_t) n])
                hi |= (uint64_t) 1 << (n - 64);
    }

    bool isSounding (int note) const noexcept
    {
        return note >= 0 && note < 128 && sounding[(size_t) note];
    }

    bool isPending (int note) const noexcept
    {
        for (int i = 0; i < numPending; ++i)
            if (pending[(size_t) i].note == note)
                return true;
        return false;
    }

private:
    struct GroupNote { int note = 0; int channel = 1; juce::uint8 velocity = 100; };
    struct Pending   { int64_t due = 0; int note = 0; int channel = 1; juce::uint8 velocity = 100; };

    void flushGroup (int pos, juce::MidiBuffer& out) noexcept
    {
        if (groupSize == 0)
            return;

        // Orden: ASC = de grave a agudo, DESC = de agudo a grave (inserción, máx. 128)
        for (int i = 1; i < groupSize; ++i)
        {
            const auto key = group[(size_t) i];
            int j = i - 1;
            while (j >= 0 && (strumDescending ? group[(size_t) j].note < key.note
                                              : group[(size_t) j].note > key.note))
            {
                group[(size_t) (j + 1)] = group[(size_t) j];
                --j;
            }
            group[(size_t) (j + 1)] = key;
        }

        for (int i = 0; i < groupSize; ++i)
        {
            const auto& g = group[(size_t) i];
            const auto delay = (int64_t) std::llround (strumOffsetMs (i, strumMs) * 0.001 * sampleRate);
            if (delay <= 0 || numPending >= kMaxPending)
                emitNoteOn (g.note, g.channel, g.velocity, pos, out);
            else
                pending[(size_t) numPending++] = { sampleClock + pos + delay, g.note, g.channel, g.velocity };
        }
        groupSize = 0;
    }

    // Emite los NoteOn pendientes que vencen antes de 'limit' (muestra absoluta)
    void emitDue (int64_t limit, juce::MidiBuffer& out) noexcept
    {
        for (int i = 0; i < numPending;)
        {
            const auto p = pending[(size_t) i];
            if (p.due < limit)
            {
                const int pos = (int) juce::jmax ((int64_t) 0, p.due - sampleClock);
                emitNoteOn (p.note, p.channel, p.velocity, pos, out);
                pending[(size_t) i] = pending[(size_t) --numPending];
            }
            else
            {
                ++i;
            }
        }
    }

    void emitNoteOn (int note, int channel, juce::uint8 velocity, int pos, juce::MidiBuffer& out) noexcept
    {
        if (note < 0 || note > 127)
            return;
        out.addEvent (juce::MidiMessage::noteOn (channel, note, velocity), pos);
        sounding[(size_t) note] = true;
        soundingChannel[(size_t) note] = channel;
    }

    // Soltar antes de tiempo: cancela lo pendiente y apaga lo que ya suena
    void handleNoteOff (int note, int channel, int pos, juce::MidiBuffer& out) noexcept
    {
        if (note < 0 || note > 127)
            return;

        bool cancelled = false;
        for (int i = 0; i < numPending;)
        {
            if (pending[(size_t) i].note == note)
            {
                pending[(size_t) i] = pending[(size_t) --numPending];
                cancelled = true;
            }
            else
            {
                ++i;
            }
        }

        if (sounding[(size_t) note])
        {
            out.addEvent (juce::MidiMessage::noteOff (soundingChannel[(size_t) note], note, (juce::uint8) 0), pos);
            sounding[(size_t) note] = false;
        }
        else if (! cancelled)
        {
            out.addEvent (juce::MidiMessage::noteOff (channel, note, (juce::uint8) 0), pos);
        }
    }

    void cancelPendingOnChannel (int channel) noexcept
    {
        for (int i = 0; i < numPending;)
        {
            if (channel <= 0 || pending[(size_t) i].channel == channel)
                pending[(size_t) i] = pending[(size_t) --numPending];
            else
                ++i;
        }
    }

    double sampleRate = 44100.0;
    double strumMs = 0.0;
    bool strumDescending = false;
    int64_t sampleClock = 0;

    std::array<GroupNote, (size_t) kMaxGroup> group {};
    int groupSize = 0;

    std::array<Pending, (size_t) kMaxPending> pending {};
    int numPending = 0;

    std::array<bool, 128> sounding {};
    std::array<int, 128> soundingChannel {};
};

} // namespace smartkeys
