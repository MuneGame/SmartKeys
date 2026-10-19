/*
  ==============================================================================

    VirtualMidiPort.h
    SmartKeys — puerto MIDI virtual "SmartKeys Chord Controller".

    El plugin crea un puerto MIDI del sistema (CoreMIDI en macOS, ALSA en
    Linux) que el DAW ve como un teclado MIDI más. Por él salen los acordes
    completos de SmartKeys (NoteOn / NoteOff de las 3–7 notas, con el desfase
    del STRUM). Así FL Studio graba en el Piano Roll del canal seleccionado
    (FLEX, Serum, Kontakt...) todas las notas del acorde, como si se hubieran
    pulsado todas las teclas a la vez, sin abrir ninguna aplicación externa.

    Tiempo real:
      - El hilo de audio sólo escribe en un FIFO lock-free de tamaño fijo
        (VirtualMidiFeed): ni candados, ni memoria dinámica, ni CoreMIDI.
      - Un hilo propio ("SmartKeys MIDI Port") vacía los FIFO cada ~1 ms y
        envía cada evento en su momento (marca de tiempo en ms del reloj de
        alta resolución), conservando el desfase del STRUM.
      - Un único puerto por proceso, compartido por todas las instancias
        (juce::SharedResourcePointer).

    Realimentación: si el DAW devuelve el puerto al propio SmartKeys (su canal
    seleccionado), las notas que regresan se reconocen como eco y se ignoran;
    así nunca se forma un bucle de acordes.

  ==============================================================================
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>

namespace smartkeys
{

//==============================================================================
struct VirtualMidiEvent
{
    double  dueMs = 0.0;        // juce::Time::getMillisecondCounterHiRes()
    uint8_t status = 0, data1 = 0, data2 = 0;
};

//==============================================================================
// Una instancia del plugin -> puerto. Productor: hilo de audio. Consumidor: el
// hilo del puerto (o el de mensajes al conectar/desconectar, bajo su candado).
class VirtualMidiFeed
{
public:
    static constexpr int    kCapacity     = 4096;
    static constexpr double kEchoWindowMs = 150.0;   // el eco de FL llega en 1–2 bloques de audio

    VirtualMidiFeed() noexcept
    {
        for (auto& s : sounding) s.store (0);
        for (auto& t : lastOnMs) t.store (-1.0e12);
    }

    // Hilo de audio. Si el FIFO está lleno el evento se descarta (nunca bloquea).
    bool push (const VirtualMidiEvent& e) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) { buffer[(size_t) scope.startIndex1] = e; return true; }
        if (scope.blockSize2 > 0) { buffer[(size_t) scope.startIndex2] = e; return true; }
        return false;
    }

    // Hilo de audio: ¿esta nota de entrada es el eco de una que SmartKeys acaba
    // de enviar por el puerto (y que sigue sonando)?
    bool isEcho (int note, double nowMs) const noexcept
    {
        if (note < 0 || note > 127)
            return false;
        return sounding[(size_t) note].load (std::memory_order_acquire) > 0
            && nowMs - lastOnMs[(size_t) note].load (std::memory_order_acquire) < kEchoWindowMs;
    }

    // Cualquier hilo: notas de esta instancia encendidas ahora en el puerto
    int getNumSoundingNotes() const noexcept
    {
        int n = 0;
        for (auto& s : sounding)
            n += s.load (std::memory_order_relaxed) > 0 ? 1 : 0;
        return n;
    }

private:
    friend class VirtualMidiPort;

    template <typename Fn>
    void drain (Fn&& fn)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) fn (buffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) fn (buffer[(size_t) (scope.startIndex2 + i)]);
    }

    juce::AbstractFifo fifo { kCapacity };
    std::array<VirtualMidiEvent, (size_t) kCapacity> buffer {};

    // Notas que esta instancia dejó encendidas en el puerto (canal x nota).
    // Sólo las toca quien tiene el candado del puerto.
    std::array<uint8_t, 16 * 128> onCount {};

    // Para reconocer el eco: los escribe el hilo del puerto, los lee el de audio
    std::array<std::atomic<int>, 128>    sounding;
    std::array<std::atomic<double>, 128> lastOnMs;
};

//==============================================================================
class VirtualMidiPort : private juce::Thread
{
public:
    // SmartKeys y SmartKeys FX son binarios distintos: cada uno crea su propio
    // puerto, con nombres distintos para que el DAW no vea dos iguales.
   #if defined (SMARTKEYS_FX_VARIANT) && SMARTKEYS_FX_VARIANT
    static constexpr const char* kPortName = "SmartKeys FX Chord Controller";
   #else
    static constexpr const char* kPortName = "SmartKeys Chord Controller";
   #endif

    using Sink = std::function<void (const juce::MidiMessage&)>;

    // Hilo de mensajes (lo crea juce::SharedResourcePointer)
    VirtualMidiPort() : juce::Thread ("SmartKeys MIDI Port")
    {
        open();
    }

    // Pruebas: en lugar de un puerto del sistema, cada mensaje va a 'sink'
    explicit VirtualMidiPort (Sink sink) : juce::Thread ("SmartKeys MIDI Port (test)"), testSink (std::move (sink))
    {
        startThread (juce::Thread::Priority::high);
    }

    ~VirtualMidiPort() override
    {
        stopThread (2000);
        const juce::ScopedLock sl (lock);
        for (auto* f : feeds)
            releaseNotes (*f);
        feeds.clear();
        numPending = 0;
        output.reset();
    }

    // Puerto creado (en Windows los plugins no pueden crear puertos virtuales)
    bool isAvailable() const noexcept     { return output != nullptr || testSink != nullptr; }

    // Hilo de mensajes: si el sistema no dejó crear el puerto (casi siempre porque
    // otro proceso, como la app SmartKeys u otro DAW con SmartKeys, ya tiene uno
    // con el mismo nombre), lo vuelve a intentar. Devuelve si ya está disponible.
    bool retryOpen()
    {
        if (! isAvailable())
            open();
        return isAvailable();
    }

    // Hilo de mensajes: conectar / desconectar una instancia
    void addFeed (VirtualMidiFeed& feed)
    {
        const juce::ScopedLock sl (lock);
        feed.drain ([] (const VirtualMidiEvent&) {});   // descarta lo que quedara de antes
        feeds.addIfNotAlreadyThere (&feed);
    }

    void removeFeed (VirtualMidiFeed& feed)
    {
        const juce::ScopedLock sl (lock);
        if (! feeds.contains (&feed))
            return;

        int kept = 0;
        for (int i = 0; i < numPending; ++i)
            if (pending[(size_t) i].feed != &feed)
                pending[(size_t) kept++] = pending[(size_t) i];
        numPending = kept;

        releaseNotes (feed);   // ninguna nota queda colgada en el DAW
        feeds.removeAllInstancesOf (&feed);
    }

private:
    struct Pending
    {
        double dueMs = 0.0;
        VirtualMidiFeed* feed = nullptr;
        uint8_t status = 0, data1 = 0, data2 = 0;
    };
    static constexpr int kMaxPending = 8192;
    static constexpr double kStaleMs = 250.0;   // NoteOn más viejos que esto ya no se envían

    // Hilo de mensajes. Sólo se llama mientras el puerto NO está disponible, es
    // decir, con el hilo del puerto parado: nadie más lee 'output' ni 'testSink'.
    void open()
    {
       #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        output = juce::MidiOutput::createNewDevice (kPortName);
       #endif

        // Pruebas automáticas en máquinas sin MIDI del sistema (servidores de CI):
        // el puerto funciona igual pero los mensajes no salen a ningún sitio
        if (output == nullptr && juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TEST_VIRTUAL_PORT", {}).isNotEmpty())
            testSink = [] (const juce::MidiMessage&) {};

        if (isAvailable() && ! isThreadRunning())
            startThread (juce::Thread::Priority::high);
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (lock);
                const double now = juce::Time::getMillisecondCounterHiRes();
                for (auto* f : feeds)
                    f->drain ([this, f, now] (const VirtualMidiEvent& e) { schedule (*f, e, now); });
                sendDue (now);
            }
            wait (1.0);
        }
    }

    void schedule (VirtualMidiFeed& feed, const VirtualMidiEvent& e, double now)
    {
        if (numPending >= kMaxPending)
        {
            // Cola llena (no debería pasar): se envía ya lo más antiguo para hacer sitio
            send (pending[0]);
            for (int i = 1; i < numPending; ++i)
                pending[(size_t) (i - 1)] = pending[(size_t) i];
            --numPending;
        }

        Pending p { e.dueMs, &feed, e.status, e.data1, e.data2 };
        if (isNoteOn (p) && p.dueMs < now - kStaleMs)
            return;

        // Inserción ordenada por tiempo (los eventos llegan casi siempre en orden)
        int i = numPending;
        while (i > 0 && pending[(size_t) (i - 1)].dueMs > p.dueMs)
        {
            pending[(size_t) i] = pending[(size_t) (i - 1)];
            --i;
        }
        pending[(size_t) i] = p;
        ++numPending;
    }

    void sendDue (double now)
    {
        int sent = 0;
        while (sent < numPending && pending[(size_t) sent].dueMs <= now)
            send (pending[(size_t) sent++]);

        if (sent > 0)
        {
            for (int i = sent; i < numPending; ++i)
                pending[(size_t) (i - sent)] = pending[(size_t) i];
            numPending -= sent;
        }
    }

    static bool isNoteOn (const Pending& p) noexcept   { return (p.status & 0xF0) == 0x90 && p.data2 > 0; }
    static bool isNoteOff (const Pending& p) noexcept  { return (p.status & 0xF0) == 0x80 || ((p.status & 0xF0) == 0x90 && p.data2 == 0); }

    void deliver (const juce::MidiMessage& m)
    {
        if (output != nullptr)
            output->sendMessageNow (m);
        else if (testSink != nullptr)
            testSink (m);
    }

    void send (const Pending& p)
    {
        if (! isAvailable() || p.feed == nullptr || p.data1 > 127)
            return;

        auto& feed = *p.feed;
        const int channel = p.status & 0x0F;
        const int note = p.data1;
        auto& count = feed.onCount[(size_t) (channel * 128 + note)];

        if (isNoteOn (p))
        {
            if (count < 255)
                ++count;
            feed.lastOnMs[(size_t) note].store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
            feed.sounding[(size_t) note].fetch_add (1, std::memory_order_acq_rel);
            deliver (juce::MidiMessage::noteOn (channel + 1, note, (juce::uint8) juce::jlimit (1, 127, (int) p.data2)));
        }
        else if (isNoteOff (p))
        {
            if (count == 0)
                return;   // el DAW nunca recibió su NoteOn (el puerto se activó con la nota ya sonando)
            --count;
            feed.sounding[(size_t) note].fetch_sub (1, std::memory_order_acq_rel);
            deliver (juce::MidiMessage::noteOff (channel + 1, note));
        }
    }

    // Apaga en el DAW todo lo que esta instancia dejó encendido
    void releaseNotes (VirtualMidiFeed& feed)
    {
        for (int i = 0; i < 16 * 128; ++i)
        {
            if (feed.onCount[(size_t) i] > 0)
            {
                feed.onCount[(size_t) i] = 0;
                deliver (juce::MidiMessage::noteOff (i / 128 + 1, i % 128));
            }
        }
        for (auto& s : feed.sounding)
            s.store (0);
    }

    std::unique_ptr<juce::MidiOutput> output;
    Sink testSink;
    juce::CriticalSection lock;                   // nunca lo toma el hilo de audio
    juce::Array<VirtualMidiFeed*> feeds;
    std::array<Pending, (size_t) kMaxPending> pending {};
    int numPending = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualMidiPort)
};

} // namespace smartkeys
