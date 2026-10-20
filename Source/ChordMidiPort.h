/*
  ==============================================================================

    ChordMidiPort.h
    SmartKeys — teclado MIDI virtual "SmartKeys Chords" (macOS).

    FL Studio graba en el Piano Roll lo que entra por un DISPOSITIVO MIDI (un
    teclado), nunca lo que un plugin manda por su salida MIDI. Por eso SmartKeys
    es también un teclado MIDI del sistema: por "SmartKeys Chords" salen los
    acordes completos (NoteOn / NoteOff de las 3–7 notas, con el desfase del
    STRUM). El instrumento que FL enganche a ese teclado ("Receive notes from")
    suena con los acordes y, al grabar, los dibuja apilados en su Piano Roll.

    Tiempo real:
      - El hilo de audio sólo escribe en un FIFO lock-free de tamaño fijo
        (ChordPortFeed): ni candados, ni memoria dinámica, ni CoreMIDI.
      - Un hilo propio vacía los FIFO cada ~1 ms y envía cada evento en su
        instante (reloj de alta resolución), conservando el STRUM.
      - Un único teclado por proceso, compartido por todas las instancias
        (juce::SharedResourcePointer). La app Standalone usa otro nombre para
        no chocar con el plugin abierto en el DAW.

    Eco: si el DAW devuelve "SmartKeys Chords" al propio SmartKeys, el
    procesador reconoce esas notas por lo que este teclado envió (cuántas veces
    y a qué hora cada nota, de TODAS las instancias) y las ignora: nunca se
    forma un bucle de acordes.

    Windows: los plugins no pueden crear teclados MIDI virtuales.

  ==============================================================================
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace smartkeys
{

//==============================================================================
struct ChordPortEvent
{
    double  dueMs = 0.0;        // juce::Time::getMillisecondCounterHiRes()
    uint8_t status = 0, data1 = 0, data2 = 0;
};

//==============================================================================
// Una instancia del plugin -> teclado. Productor: hilo de audio. Consumidor: el
// hilo del teclado (o el de mensajes al conectar/desconectar, con su candado).
class ChordPortFeed
{
public:
    static constexpr int kCapacity = 4096;

    ChordPortFeed() noexcept
    {
        for (auto& t : lastNoteOnSentMs)
            t.store (-1.0e12);
        for (auto& t : lastNoteOffSentMs)
            t.store (-1.0e12);
        for (auto& c : noteOnSerial)
            c.store (0);
        for (auto& c : noteOffSerial)
            c.store (0);
    }

    // Hilo de audio. Si el FIFO está lleno el evento se descarta (nunca bloquea).
    bool push (const ChordPortEvent& e) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) { buffer[(size_t) scope.startIndex1] = e; return true; }
        if (scope.blockSize2 > 0) { buffer[(size_t) scope.startIndex2] = e; return true; }
        return false;
    }

    // Hilo de audio: lo que el teclado envió de 'note' (de cualquier instancia).
    //   Serial = cuántos NoteOn / NoteOff lleva enviados; Ms = cuándo el último.
    //   Leer primero el serial: la hora leída después es la de ese envío o una posterior.
    uint32_t getNoteOnSerial (int note) const noexcept
    {
        return (note >= 0 && note < 128) ? noteOnSerial[(size_t) note].load (std::memory_order_acquire) : 0u;
    }

    uint32_t getNoteOffSerial (int note) const noexcept
    {
        return (note >= 0 && note < 128) ? noteOffSerial[(size_t) note].load (std::memory_order_acquire) : 0u;
    }

    double getLastNoteOnSentMs (int note) const noexcept
    {
        return (note >= 0 && note < 128) ? lastNoteOnSentMs[(size_t) note].load (std::memory_order_acquire) : -1.0e12;
    }

    double getLastNoteOffSentMs (int note) const noexcept
    {
        return (note >= 0 && note < 128) ? lastNoteOffSentMs[(size_t) note].load (std::memory_order_acquire) : -1.0e12;
    }

    // Cualquier hilo: apagar todo lo que esta instancia dejó encendido y tirar lo pendiente
    void requestReleaseAll() noexcept         { releaseRequested.store (true, std::memory_order_release); }

    // Notas que esta instancia tiene encendidas ahora en el teclado
    int getNumSoundingNotes() const noexcept  { return soundingNotes.load (std::memory_order_relaxed); }

private:
    friend class ChordMidiPort;

    juce::AbstractFifo fifo { kCapacity };
    std::array<ChordPortEvent, (size_t) kCapacity> buffer {};

    std::array<uint8_t, 16 * 128> onCount {};                  // sólo con el candado del teclado
    std::array<std::atomic<double>, 128> lastNoteOnSentMs;      // escribe el hilo del teclado
    std::array<std::atomic<double>, 128> lastNoteOffSentMs;
    std::array<std::atomic<uint32_t>, 128> noteOnSerial;
    std::array<std::atomic<uint32_t>, 128> noteOffSerial;
    std::atomic<bool> releaseRequested { false };
    std::atomic<int> soundingNotes { 0 };
};

//==============================================================================
class ChordMidiPort : private juce::Thread
{
public:
    // Lo crea juce::SharedResourcePointer en el hilo de mensajes
    ChordMidiPort();
    ~ChordMidiPort() override;

    // "SmartKeys Chords" (plugin en el DAW) o "SmartKeys Chords (App)" (Standalone).
    // El procesador indica el tipo de proceso antes de crear el teclado.
    static void setStandaloneProcess (bool standalone) noexcept;
    static juce::String getDeviceName();

    bool isAvailable() const noexcept;

    // Hilo de mensajes: si el sistema no dejó crear el teclado (otro proceso ya
    // tiene uno con el mismo nombre), lo vuelve a intentar.
    bool retryOpen();

    // Hilo de mensajes: conectar / desconectar una instancia
    void addFeed (ChordPortFeed& feed);
    void removeFeed (ChordPortFeed& feed);   // envía los NoteOff de lo que quedara sonando

    static constexpr double kStaleMs = 250.0;   // NoteOn más viejos que esto ya no se envían

private:
    struct Pending
    {
        double dueMs = 0.0;
        ChordPortFeed* feed = nullptr;
        uint8_t status = 0, data1 = 0, data2 = 0;
    };
    static constexpr int kMaxPending = 8192;

    void open();
    void run() override;
    void schedule (ChordPortFeed& feed, const ChordPortEvent& e, double now);
    void sendDue (double now);
    void send (const Pending& p);
    void deliver (uint8_t status, uint8_t data1, uint8_t data2);
    void recordSent (bool noteOn, int note);
    void dropPending (ChordPortFeed& feed);
    void releaseNotes (ChordPortFeed& feed);

    template <typename Fn>
    static void drain (ChordPortFeed& feed, Fn&& fn)
    {
        const auto scope = feed.fifo.read (feed.fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) fn (feed.buffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) fn (feed.buffer[(size_t) (scope.startIndex2 + i)]);
    }

    std::unique_ptr<juce::MidiOutput> output;
    bool testSink = false;                       // pruebas en Linux sin MIDI del sistema
    std::FILE* testFile = nullptr;

    // macOS: mientras exista el teclado, el sistema no "duerme" el proceso (App Nap
    // agruparía los temporizadores y las notas saldrían tarde)
    std::unique_ptr<juce::ScopedLowPowerModeDisabler> keepAwake;

    // Diagnóstico (SMARTKEYS_PORT_DEBUG=1): con qué retraso salen las notas
    bool debug = false, realtime = false;
    double debugNextReportMs = 0.0, debugMaxLateMs = 0.0, debugSumLateMs = 0.0;
    int debugSent = 0, debugLate = 0;

    juce::CriticalSection lock;                  // nunca lo toma el hilo de audio
    juce::Array<ChordPortFeed*> feeds;
    std::array<Pending, (size_t) kMaxPending> pending {};
    int numPending = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMidiPort)
};

} // namespace smartkeys
