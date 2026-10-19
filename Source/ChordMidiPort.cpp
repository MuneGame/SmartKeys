/*
  ==============================================================================

    ChordMidiPort.cpp
    SmartKeys — teclado MIDI virtual "SmartKeys Chords" (ver ChordMidiPort.h).

  ==============================================================================
*/

#include "ChordMidiPort.h"

namespace smartkeys
{

namespace
{
    // Hilo de mensajes: el procesador lo fija antes de crear el teclado
    std::atomic<bool> isStandaloneProcess { false };

    bool isNoteOnStatus (uint8_t status, uint8_t velocity) noexcept   { return (status & 0xF0) == 0x90 && velocity > 0; }
    bool isNoteOffStatus (uint8_t status, uint8_t velocity) noexcept
    {
        return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && velocity == 0);
    }
}

void ChordMidiPort::setStandaloneProcess (bool standalone) noexcept
{
    isStandaloneProcess.store (standalone);
}

juce::String ChordMidiPort::getDeviceName()
{
    return isStandaloneProcess.load() ? "SmartKeys Chords (App)" : "SmartKeys Chords";
}

//==============================================================================
ChordMidiPort::ChordMidiPort() : juce::Thread ("SmartKeys Chords")
{
    open();
}

ChordMidiPort::~ChordMidiPort()
{
    stopThread (2000);

    const juce::ScopedLock sl (lock);
    for (auto* f : feeds)
        releaseNotes (*f);
    feeds.clear();
    numPending = 0;
    output.reset();

    if (testFile != nullptr)
    {
        std::fclose (testFile);
        testFile = nullptr;
    }
}

bool ChordMidiPort::isAvailable() const noexcept
{
    return output != nullptr || testSink;
}

// Hilo de mensajes. Sólo se llama mientras el teclado NO está disponible, es
// decir, con su hilo parado: nadie más lee 'output' ni 'testSink'.
void ChordMidiPort::open()
{
   #if JUCE_LINUX
    // Pruebas automáticas (Linux): el teclado funciona igual pero escribe lo que
    // enviaría en un archivo ("1" = a ningún sitio), con su hora de envío
    const auto spec = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TEST_CHORD_PORT", {});
    if (spec.isNotEmpty())
    {
        testSink = true;
        if (spec != "1" && testFile == nullptr)
            testFile = std::fopen (spec.toRawUTF8(), "w");
    }
    else
    {
        output = juce::MidiOutput::createNewDevice (getDeviceName());
    }
   #elif JUCE_MAC || JUCE_BSD
    output = juce::MidiOutput::createNewDevice (getDeviceName());
   #endif

    // Hilo de tiempo real (como el de un reloj MIDI): se despierta a su hora aunque
    // el sistema agrupe temporizadores. Si el sistema no lo permite, prioridad alta.
    if (isAvailable() && ! isThreadRunning())
        if (! startRealtimeThread (juce::Thread::RealtimeOptions{}.withPeriodMs (1.0).withMaximumProcessingTimeMs (0.5)))
            startThread (juce::Thread::Priority::high);
}

bool ChordMidiPort::retryOpen()
{
    if (! isAvailable())
        open();
    return isAvailable();
}

//==============================================================================
void ChordMidiPort::addFeed (ChordPortFeed& feed)
{
    const juce::ScopedLock sl (lock);
    drain (feed, [] (const ChordPortEvent&) {});   // descarta lo que quedara de antes
    feed.releaseRequested.store (false);
    feeds.addIfNotAlreadyThere (&feed);
}

void ChordMidiPort::removeFeed (ChordPortFeed& feed)
{
    const juce::ScopedLock sl (lock);
    if (! feeds.contains (&feed))
        return;

    dropPending (feed);
    releaseNotes (feed);   // ninguna nota queda colgada en el DAW
    feeds.removeAllInstancesOf (&feed);
}

//==============================================================================
void ChordMidiPort::run()
{
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (lock);
            const double now = juce::Time::getMillisecondCounterHiRes();

            for (auto* f : feeds)
            {
                if (f->releaseRequested.exchange (false, std::memory_order_acq_rel))
                {
                    // El procesador se reinició o quedó en bypass: nada pendiente, todo apagado
                    drain (*f, [] (const ChordPortEvent&) {});
                    dropPending (*f);
                    releaseNotes (*f);
                    continue;
                }

                drain (*f, [this, f, now] (const ChordPortEvent& e) { schedule (*f, e, now); });
            }

            sendDue (now);
        }

        wait (1.0);
    }
}

void ChordMidiPort::schedule (ChordPortFeed& feed, const ChordPortEvent& e, double now)
{
    Pending p { e.dueMs, &feed, e.status, e.data1, e.data2 };

    // Un NoteOn que llega con mucho retraso (el sistema estuvo bloqueado) ya no se envía;
    // su NoteOff tampoco saldrá (no hay nada que apagar)
    if (isNoteOnStatus (p.status, p.data2) && p.dueMs < now - kStaleMs)
        return;

    if (numPending >= kMaxPending)
    {
        // Cola llena (no debería pasar): se envía ya lo más antiguo para hacer sitio
        send (pending[0]);
        for (int i = 1; i < numPending; ++i)
            pending[(size_t) (i - 1)] = pending[(size_t) i];
        --numPending;
    }

    // Inserción ordenada por tiempo (casi siempre llegan en orden: inserción al final)
    int i = numPending;
    while (i > 0 && pending[(size_t) (i - 1)].dueMs > p.dueMs)
    {
        pending[(size_t) i] = pending[(size_t) (i - 1)];
        --i;
    }
    pending[(size_t) i] = p;
    ++numPending;
}

void ChordMidiPort::sendDue (double now)
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

void ChordMidiPort::dropPending (ChordPortFeed& feed)
{
    int kept = 0;
    for (int i = 0; i < numPending; ++i)
        if (pending[(size_t) i].feed != &feed)
            pending[(size_t) kept++] = pending[(size_t) i];
    numPending = kept;
}

void ChordMidiPort::deliver (uint8_t status, uint8_t data1, uint8_t data2)
{
    if (output != nullptr)
    {
        output->sendMessageNow (juce::MidiMessage ((int) status, (int) data1, (int) data2));
    }
    else if (testFile != nullptr)
    {
        std::fprintf (testFile, "%.3f %d %d %d\n", juce::Time::getMillisecondCounterHiRes(), (int) status, (int) data1, (int) data2);
        std::fflush (testFile);
    }
}

void ChordMidiPort::send (const Pending& p)
{
    if (! isAvailable() || p.feed == nullptr || p.data1 > 127)
        return;

    auto& feed = *p.feed;
    const int channel = p.status & 0x0F;
    const int note = p.data1;
    auto& count = feed.onCount[(size_t) (channel * 128 + note)];

    if (isNoteOnStatus (p.status, p.data2))
    {
        if (count < 255)
            ++count;
        feed.soundingNotes.fetch_add (1, std::memory_order_relaxed);
        recordSent (true, note);
        deliver ((uint8_t) (0x90 | channel), (uint8_t) note, (uint8_t) juce::jlimit (1, 127, (int) p.data2));
    }
    else if (isNoteOffStatus (p.status, p.data2))
    {
        if (count == 0)
            return;   // su NoteOn nunca salió (llegó tarde o el teclado se conectó con la nota ya sonando)
        --count;
        feed.soundingNotes.fetch_sub (1, std::memory_order_relaxed);
        recordSent (false, note);
        deliver ((uint8_t) (0x80 | channel), (uint8_t) note, 0);
    }
}

// Lo que sale por el teclado lo conocen TODAS las instancias: si el DAW lo devuelve
// a cualquiera de ellas (la seleccionada en FL Studio), ésa lo reconoce como eco.
// Primero la hora y después el serial (release): quien lee el serial ve esa hora.
void ChordMidiPort::recordSent (bool noteOn, int note)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto* f : feeds)
    {
        if (noteOn)
        {
            f->lastNoteOnSentMs[(size_t) note].store (now, std::memory_order_relaxed);
            f->noteOnSerial[(size_t) note].fetch_add (1, std::memory_order_release);
        }
        else
        {
            f->lastNoteOffSentMs[(size_t) note].store (now, std::memory_order_relaxed);
            f->noteOffSerial[(size_t) note].fetch_add (1, std::memory_order_release);
        }
    }
}

// Apaga en el DAW todo lo que esta instancia dejó encendido
void ChordMidiPort::releaseNotes (ChordPortFeed& feed)
{
    for (int i = 0; i < 16 * 128; ++i)
    {
        if (feed.onCount[(size_t) i] > 0)
        {
            feed.onCount[(size_t) i] = 0;
            recordSent (false, i % 128);
            deliver ((uint8_t) (0x80 | (i / 128)), (uint8_t) (i % 128), 0);
        }
    }
    feed.soundingNotes.store (0, std::memory_order_relaxed);
}

} // namespace smartkeys
