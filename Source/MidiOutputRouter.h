/*
  ==============================================================================

    MidiOutputRouter.h
    SmartKeys — Traducción de notas disparadoras a notas/acordes de salida y
    registro anti-notas colgadas.

    - Cada NoteOn disparador (MIDI, teclado en pantalla o voz) se convierte en
      1 nota (SINGLE NOTES), 3-4 notas (SMART CHORDS) o 5-7 notas (COMPLEX
      CHORDS) que se escriben en el MidiBuffer de salida -> el DAW recibe el
      acorde completo y el sintetizador interno lo toca.
    - COMPLEX CHORDS aplica "velocity voicing": cada nota sale con su propia
      velocidad (mano izquierda 95 %, voces centrales 80 %, voz aguda 90 %).
    - ActiveNoteRegistry recuerda exactamente qué notas disparó cada tecla, de
      modo que su NoteOff libera esas mismas notas aunque el usuario haya
      cambiado de escala, tónica, modo o variación mientras la mantenía.
    - Conteo de referencias por nota de salida: si dos acordes comparten una
      nota (C-E-G y A-C-E), soltar uno no corta la nota que el otro sostiene.
    - Cada acorde recibe un identificador único: el capturador de la
      progresión lo usa para guardar el acorde completo (todas sus notas, con
      su posición y duración exactas) aunque comparta notas con otro.
    - Acordes explícitos (paleta de acordes y vista previa de la progresión)
      con sus propias ranuras, que pasan por el mismo conteo de referencias.
    - Sin asignaciones de memoria en el hilo de audio: todos los vectores
      tienen capacidad reservada en el constructor y el voicing usa arrays fijos.

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "ScaleChordEngine.h"

namespace smartkeys
{

//==============================================================================
class ActiveNoteRegistry
{
public:
    static constexpr size_t kReserve = 16;

    ActiveNoteRegistry()
    {
        for (auto& v : activeChordsByTriggerNote)
            v.reserve (kReserve);
        voiceChord.reserve (kReserve);
        paletteChord.reserve (kReserve);
        previewChord.reserve (kReserve);
        clear();
    }

    void clear() noexcept
    {
        for (auto& v : activeChordsByTriggerNote)
            v.clear();
        voiceChord.clear();
        paletteChord.clear();
        previewChord.clear();
        triggerChannel.fill (1);
        triggerChordId.fill (0);
        voiceChannel = 1;
        voiceChordId = paletteChordId = previewChordId = 0;
        outputRefCount.fill (0);
        outputChannel.fill (1);
    }

    // Notas de acorde disparadas por cada tecla de entrada (0..127)
    std::array<std::vector<int>, 128> activeChordsByTriggerNote;
    std::array<int, 128> triggerChannel {};
    std::array<uint32_t, 128> triggerChordId {};

    // Ranura independiente para el controlador vocal
    std::vector<int> voiceChord;
    int voiceChannel = 1;
    uint32_t voiceChordId = 0;

    // Ranuras de acordes explícitos: paleta de acordes (escucha) y vista previa de la progresión
    std::vector<int> paletteChord, previewChord;
    uint32_t paletteChordId = 0, previewChordId = 0;

    // Conteo de referencias y canal de cada nota de salida
    std::array<uint16_t, 128> outputRefCount {};
    std::array<int, 128> outputChannel {};
};

//==============================================================================
class MidiOutputRouter
{
public:
    enum class Source : int { Midi = 0, Screen = 1, Voice = 2, Palette = 3, Preview = 4 };

    static constexpr int kMaxPackedNotes = 7;
    static constexpr int kMaxChordNotes  = ScaleChordEngine::kMaxChordNotes;

    // Acorde completo generado por una tecla (para el capturador de la progresión)
    struct ChordTriggerInfo
    {
        uint32_t chordId = 0;            // identificador único del acorde (0 = ninguno)
        uint32_t releasedChordId = 0;    // acorde anterior liberado por un re-disparo
        int count = 0;
        std::array<int, kMaxChordNotes> notes {};
        std::array<juce::uint8, kMaxChordNotes> velocities {};
    };

    MidiOutputRouter() = default;

    void reset() noexcept
    {
        registry.clear();
        lastEventPacked.store (0);
        lastVoiceEventPacked.store (0);
    }

    uint32_t getLastChordId() const noexcept { return chordIdCounter; }

    //==========================================================================
    // NoteOn de una nota disparadora -> NoteOn de todas las notas generadas
    // Devuelve el id del acorde generado; 'info' (opcional) recibe todas sus notas.
    uint32_t handleNoteOn (Source source, int triggerNote, int channel, juce::uint8 velocity,
                           int samplePosition, const ScaleChordEngine& engine, juce::MidiBuffer& out,
                           ChordTriggerInfo* info = nullptr)
    {
        triggerNote = juce::jlimit (0, 127, triggerNote);
        channel = juce::jlimit (1, 16, channel);
        const int inputVelocity = juce::jlimit (1, 127, (int) velocity);

        auto& slot = (source == Source::Voice) ? registry.voiceChord
                                               : registry.activeChordsByTriggerNote[(size_t) triggerNote];
        auto& slotId = (source == Source::Voice) ? registry.voiceChordId
                                                 : registry.triggerChordId[(size_t) triggerNote];

        if (info != nullptr)
        {
            info->count = 0;
            info->releasedChordId = 0;
        }

        // Re-disparo sin NoteOff previo: liberar primero el acorde anterior
        if (! slot.empty())
        {
            if (info != nullptr)
                info->releasedChordId = slotId;
            releaseSlot (slot, samplePosition, out);
        }

        engine.generateVoicing (triggerNote, voicing);

        for (int i = 0; i < voicing.count; ++i)
        {
            const int note = voicing.notes[(size_t) i];
            if (note < 0 || note > 127 || slot.size() >= slot.capacity())
                continue;

            const int vel = juce::jlimit (1, 127, (int) std::lround ((float) inputVelocity
                                                                     * voicing.velocityScale[(size_t) i]));
            auto& ref = registry.outputRefCount[(size_t) note];
            if (ref == 0)
            {
                registry.outputChannel[(size_t) note] = channel;
                out.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) vel), samplePosition);
            }
            ++ref;
            slot.push_back (note);

            if (info != nullptr && info->count < kMaxChordNotes)
            {
                info->notes[(size_t) info->count] = note;
                info->velocities[(size_t) info->count] = (juce::uint8) vel;
                ++info->count;
            }
        }

        if (source == Source::Voice) registry.voiceChannel = channel;
        else                         registry.triggerChannel[(size_t) triggerNote] = channel;

        slotId = nextChordId();
        if (info != nullptr)
            info->chordId = slotId;

        publishLastEvent (source, triggerNote, slot, (int) engine.getMode());
        return slotId;
    }

    // NoteOff de la nota disparadora -> NoteOff exacto de lo que disparó.
    // Devuelve el id del acorde liberado (0 si esa tecla no sonaba).
    uint32_t handleNoteOff (Source source, int triggerNote, int samplePosition, juce::MidiBuffer& out)
    {
        triggerNote = juce::jlimit (0, 127, triggerNote);
        auto& slot = (source == Source::Voice) ? registry.voiceChord
                                               : registry.activeChordsByTriggerNote[(size_t) triggerNote];
        auto& slotId = (source == Source::Voice) ? registry.voiceChordId
                                                 : registry.triggerChordId[(size_t) triggerNote];
        const uint32_t released = slot.empty() ? 0u : slotId;
        releaseSlot (slot, samplePosition, out);
        slotId = 0;
        return released;
    }

    //==========================================================================
    // Acordes explícitos (paleta de acordes y vista previa de la progresión):
    // las notas ya vienen decididas; se registran igual que las de una tecla.
    uint32_t handleExplicitChordOn (Source source, const int* notes, const juce::uint8* velocities, int count,
                                    int channel, int samplePosition, juce::MidiBuffer& out)
    {
        auto& slot = explicitSlot (source);
        auto& slotId = explicitSlotId (source);
        channel = juce::jlimit (1, 16, channel);

        if (! slot.empty())
            releaseSlot (slot, samplePosition, out);

        for (int i = 0; i < juce::jmin (count, kMaxChordNotes); ++i)
        {
            const int note = notes[i];
            if (note < 0 || note > 127 || slot.size() >= slot.capacity())
                continue;

            auto& ref = registry.outputRefCount[(size_t) note];
            if (ref == 0)
            {
                registry.outputChannel[(size_t) note] = channel;
                out.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) juce::jlimit (1, 127, (int) velocities[i])),
                              samplePosition);
            }
            ++ref;
            slot.push_back (note);
        }

        slotId = nextChordId();
        if (! slot.empty())
            publishLastEvent (source, slot.front(), slot, kExplicitMode);
        return slotId;
    }

    void handleExplicitChordOff (Source source, int samplePosition, juce::MidiBuffer& out)
    {
        releaseSlot (explicitSlot (source), samplePosition, out);
        explicitSlotId (source) = 0;
    }

    bool isExplicitSlotActive (Source source) const noexcept
    {
        return source == Source::Preview ? ! registry.previewChord.empty() : ! registry.paletteChord.empty();
    }

    // Libera absolutamente todo (CC 120/123, pánico, reset)
    void releaseAll (int samplePosition, juce::MidiBuffer& out)
    {
        for (auto& slot : registry.activeChordsByTriggerNote)
            releaseSlot (slot, samplePosition, out);
        releaseSlot (registry.voiceChord, samplePosition, out);
        releaseSlot (registry.paletteChord, samplePosition, out);
        releaseSlot (registry.previewChord, samplePosition, out);
        registry.triggerChordId.fill (0);
        registry.voiceChordId = registry.paletteChordId = registry.previewChordId = 0;

        // Seguridad extra: cualquier nota que quedara con referencias
        for (int n = 0; n < 128; ++n)
        {
            if (registry.outputRefCount[(size_t) n] > 0)
            {
                out.addEvent (juce::MidiMessage::noteOff (registry.outputChannel[(size_t) n], n, (juce::uint8) 0), samplePosition);
                registry.outputRefCount[(size_t) n] = 0;
            }
        }
    }

    bool isVoiceSlotActive() const noexcept { return ! registry.voiceChord.empty(); }

    bool isNoteSounding (int note) const noexcept
    {
        return note >= 0 && note < 128 && registry.outputRefCount[(size_t) note] > 0;
    }

    // Bitmask de 128 bits con todas las notas de salida que suenan ahora
    void getActiveBitmask (uint64_t& lo, uint64_t& hi) const noexcept
    {
        lo = hi = 0;
        for (int n = 0; n < 64; ++n)
            if (registry.outputRefCount[(size_t) n] > 0)
                lo |= (uint64_t) 1 << n;
        for (int n = 64; n < 128; ++n)
            if (registry.outputRefCount[(size_t) n] > 0)
                hi |= (uint64_t) 1 << (n - 64);
    }

    const ActiveNoteRegistry& getRegistry() const noexcept { return registry; }

    //==========================================================================
    // Último evento empaquetado en 64 bits (para el display LED):
    //   bits 0-2   : número de notas (0..7)
    //   bits 3-51  : hasta 7 notas de 7 bits
    //   bits 52-58 : nota disparadora
    //   bit  59    : origen = voz
    //   bit  60    : válido
    //   bits 61-62 : modo (0 Smart, 1 Single, 2 Complex, 3 acorde explícito de la paleta / progresión)
    std::atomic<uint64_t> lastEventPacked { 0 };

    static constexpr int kExplicitMode = 3;

    // Último acorde disparado por la VOZ (etiqueta en vivo "Mic: A3 -> F Maj7")
    std::atomic<uint64_t> lastVoiceEventPacked { 0 };

    struct DecodedEvent
    {
        bool valid = false;
        bool fromVoice = false;
        int triggerNote = 0;
        int mode = 0;
        std::vector<int> notes;
    };

    static DecodedEvent decodeLastEvent (uint64_t packed)
    {
        DecodedEvent e;
        e.valid = ((packed >> 60) & 1u) != 0;
        if (! e.valid)
            return e;
        e.fromVoice = ((packed >> 59) & 1u) != 0;
        e.triggerNote = (int) ((packed >> 52) & 0x7Fu);
        e.mode = (int) ((packed >> 61) & 0x3u);
        const int count = (int) (packed & 0x7u);
        for (int i = 0; i < count; ++i)
            e.notes.push_back ((int) ((packed >> (3 + 7 * i)) & 0x7Fu));
        return e;
    }

private:
    uint32_t nextChordId() noexcept
    {
        if (++chordIdCounter == 0)
            chordIdCounter = 1;
        return chordIdCounter;
    }

    std::vector<int>& explicitSlot (Source source) noexcept
    {
        return source == Source::Preview ? registry.previewChord : registry.paletteChord;
    }

    uint32_t& explicitSlotId (Source source) noexcept
    {
        return source == Source::Preview ? registry.previewChordId : registry.paletteChordId;
    }

    void releaseSlot (std::vector<int>& slot, int samplePosition, juce::MidiBuffer& out)
    {
        for (int note : slot)
        {
            auto& ref = registry.outputRefCount[(size_t) note];
            if (ref > 0 && --ref == 0)
                out.addEvent (juce::MidiMessage::noteOff (registry.outputChannel[(size_t) note], note, (juce::uint8) 0),
                              samplePosition);
        }
        slot.clear();
    }

    void publishLastEvent (Source source, int triggerNote, const std::vector<int>& notes, int mode) noexcept
    {
        uint64_t packed = 0;
        const int count = juce::jmin (kMaxPackedNotes, (int) notes.size());
        packed |= (uint64_t) count;
        for (int i = 0; i < count; ++i)
            packed |= (uint64_t) (notes[(size_t) i] & 0x7F) << (3 + 7 * i);
        packed |= (uint64_t) (triggerNote & 0x7F) << 52;
        if (source == Source::Voice)
            packed |= (uint64_t) 1 << 59;
        packed |= (uint64_t) 1 << 60;
        packed |= (uint64_t) (mode & 0x3) << 61;
        lastEventPacked.store (packed, std::memory_order_release);
        if (source == Source::Voice)
            lastVoiceEventPacked.store (packed, std::memory_order_release);
    }

    ActiveNoteRegistry registry;
    ScaleChordEngine::Voicing voicing;
    uint32_t chordIdCounter = 0;
};

} // namespace smartkeys
