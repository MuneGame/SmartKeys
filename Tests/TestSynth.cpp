/*
  ==============================================================================

    TestSynth.cpp
    SmartKeys — instrumento de PRUEBA para la ranura de SmartKeys 2.0 (no se
    distribuye). Lo cargan las pruebas automáticas dentro de SmartKeys para
    comprobar lo que de verdad le llega a un instrumento huésped.

      - Suena: una senoide por nota pulsada (sin memoria dinámica).
      - Apunta cada evento MIDI que recibe con su muestra exacta
        (SMARTKEYS_TESTSYNTH_LOG=/ruta/archivo): "#<id> EV <muestra> <status> <d1> <d2>".
        También PREPARE, RELEASE, el tamaño de bloque más grande, el transporte
        del DAW, guardar / cargar su estado y cuándo se crea y se destruye.
      - SMARTKEYS_TESTSYNTH_LATENCY=<muestras>: latencia que declara.
      - SMARTKEYS_TESTSYNTH_LOAD_MS=<ms>: tarda en crearse (carga lenta).
      - CC 102 con valor 127: ese bloque devuelve NaN (SmartKeys debe silenciarlo).
      - Su estado guarda cuántas notas ha recibido y su parámetro "Tone".

  ==============================================================================
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace
{

std::atomic<int> gNextInstanceId { 1 };

class TestSynthProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    TestSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (tone = new juce::AudioParameterFloat (juce::ParameterID { "tone", 1 }, "Tone", 0.0f, 1.0f, 0.25f));

        const auto logPath = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TESTSYNTH_LOG", {});
        if (logPath.isNotEmpty())
            logFile = juce::File (logPath);

        setLatencySamples (juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TESTSYNTH_LATENCY", "0").getIntValue());

        const int loadMs = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_TESTSYNTH_LOAD_MS", "0").getIntValue();
        if (loadMs > 0)
            juce::Thread::sleep (loadMs);

        line ("CREATED latency=" + juce::String (getLatencySamples()));
        startTimer (10);
    }

    ~TestSynthProcessor() override
    {
        stopTimer();
        flush();
        line ("DESTROYED notes=" + juce::String (notesReceived.load()));
    }

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        rate = sampleRate;
        preparedBlock = samplesPerBlock;
        maxSeen = 0;
        for (auto& v : voices)
            v = {};
        flush();
        line ("PREPARE " + juce::String (sampleRate, 0) + " " + juce::String (samplesPerBlock)
              + " realtime=" + juce::String (isNonRealtime() ? 0 : 1));
    }

    void releaseResources() override
    {
        flush();
        line ("RELEASE");
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();

        if (n > maxSeen)
        {
            maxSeen = n;
            push (n > preparedBlock ? kOverBlock : kMaxBlock, clock, n, preparedBlock, 0);
        }

        // Transporte del DAW (SmartKeys le pasa el suyo)
        if (auto* ph = getPlayHead())
        {
            if (const auto pos = ph->getPosition())
            {
                const bool playing = pos->getIsPlaying();
                if (playing != lastPlaying)
                {
                    lastPlaying = playing;
                    push (kTransport, clock, playing ? 1 : 0, juce::roundToInt (pos->getBpm().orFallback (0.0)), 0);
                }
            }
        }

        bool makeNaN = false;
        int pos = 0;
        auto it = midi.cbegin();
        const auto end = midi.cend();

        auto renderTo = [&] (int until)
        {
            for (auto& v : voices)
            {
                if (v.level <= 0.0f && ! v.on)
                    continue;
                const double inc = juce::MathConstants<double>::twoPi * v.hz / rate;
                for (int i = pos; i < until; ++i)
                {
                    v.level = v.on ? juce::jmin (v.target, v.level + 0.005f) : juce::jmax (0.0f, v.level - 0.002f);
                    const float s = v.level * (float) (std::sin (v.phase) + tone->get() * 0.3 * std::sin (2.0 * v.phase));
                    v.phase += inc;
                    if (v.phase > juce::MathConstants<double>::twoPi)
                        v.phase -= juce::MathConstants<double>::twoPi;
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.addSample (ch, i, s);
                }
            }
            pos = until;
        };

        for (; it != end; ++it)
        {
            const auto meta = *it;
            const int at = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);
            renderTo (at);

            const auto* d = meta.data;
            if (d == nullptr || meta.numBytes < 1)
                continue;
            const int status = d[0];
            const int d1 = meta.numBytes > 1 ? d[1] : 0;
            const int d2 = meta.numBytes > 2 ? d[2] : 0;
            push (kEvent, clock + meta.samplePosition, status, d1, d2);

            const int type = status & 0xF0;
            if (type == 0x90 && d2 > 0)
            {
                notesReceived.fetch_add (1);
                auto& v = voices[(size_t) (d1 & 0x7F)];
                v.on = true;
                v.hz = 440.0 * std::pow (2.0, (d1 - 69) / 12.0);
                v.target = 0.08f * (float) d2 / 127.0f;
            }
            else if (type == 0x80 || type == 0x90)
            {
                voices[(size_t) (d1 & 0x7F)].on = false;
            }
            else if (type == 0xB0 && (d1 == 123 || d1 == 120))
            {
                for (auto& v : voices)
                    v.on = false;
            }
            else if (type == 0xB0 && d1 == 102 && d2 == 127)
            {
                makeNaN = true;
            }
        }
        renderTo (n);

        if (makeNaN && n > 0)
            buffer.setSample (0, n / 2, std::numeric_limits<float>::quiet_NaN());

        clock += n;
        midi.clear();
    }

    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override   { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "SmartKeysTestSynth"; }
    bool acceptsMidi() const override                     { return true; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.05; }
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        juce::XmlElement xml ("TestSynth");
        xml.setAttribute ("notes", notesReceived.load());
        xml.setAttribute ("tone", (double) tone->get());
        copyXmlToBinary (xml, destData);
        flush();
        line ("STATE_SAVE notes=" + juce::String (notesReceived.load()));
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
        {
            *tone = (float) xml->getDoubleAttribute ("tone", 0.25);
            flush();
            line ("STATE_LOAD notes=" + juce::String (xml->getIntAttribute ("notes", -1))
                  + " tone=" + juce::String (tone->get(), 3));
        }
    }

private:
    enum Kind : int { kEvent = 1, kMaxBlock, kOverBlock, kTransport };

    struct Entry { juce::int64 at = 0; int kind = 0, a = 0, b = 0, c = 0; };
    struct Voice { bool on = false; double hz = 440.0, phase = 0.0; float level = 0.0f, target = 0.0f; };

    // Hilo de audio -> FIFO de tamaño fijo -> el temporizador lo escribe en el archivo
    void push (int kind, juce::int64 at, int a, int b, int c) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        if (w - readIndex.load (std::memory_order_acquire) >= (juce::int64) ring.size())
        {
            dropped.fetch_add (1);
            return;
        }
        ring[(size_t) (w % (juce::int64) ring.size())] = { at, kind, a, b, c };
        writeIndex.store (w + 1, std::memory_order_release);
    }

    void timerCallback() override   { flush(); }

    void flush()
    {
        const juce::ScopedLock sl (fileLock);
        if (logFile == juce::File())
            return;

        juce::String text;
        const auto w = writeIndex.load (std::memory_order_acquire);
        auto r = readIndex.load (std::memory_order_relaxed);
        for (; r < w; ++r)
        {
            const auto& e = ring[(size_t) (r % (juce::int64) ring.size())];
            text << "#" << id << " ";
            switch (e.kind)
            {
                case kEvent:     text << "EV " << e.at << " " << e.a << " " << e.b << " " << e.c; break;
                case kMaxBlock:  text << "MAXBLOCK " << e.a << " prepared=" << e.b; break;
                case kOverBlock: text << "OVERBLOCK " << e.a << " prepared=" << e.b; break;
                case kTransport: text << "TRANSPORT playing=" << e.a << " bpm=" << e.b; break;
                default:         text << "?"; break;
            }
            text << "\n";
        }
        readIndex.store (r, std::memory_order_release);
        if (const auto lost = dropped.exchange (0); lost > 0)
            text << "#" << id << " DROPPED " << lost << "\n";

        if (text.isNotEmpty())
            logFile.appendText (text, false, false, "\n");
    }

    void line (const juce::String& s)
    {
        const juce::ScopedLock sl (fileLock);
        if (logFile != juce::File())
            logFile.appendText ("#" + juce::String (id) + " " + s + "\n", false, false, "\n");
    }

    const int id = gNextInstanceId.fetch_add (1);
    juce::AudioParameterFloat* tone = nullptr;
    juce::File logFile;
    juce::CriticalSection fileLock;

    double rate = 44100.0;
    int preparedBlock = 512, maxSeen = 0;
    juce::int64 clock = 0;
    bool lastPlaying = false;
    std::array<Voice, 128> voices {};
    std::atomic<int> notesReceived { 0 };

    std::array<Entry, 1 << 16> ring {};
    std::atomic<juce::int64> writeIndex { 0 }, readIndex { 0 };
    std::atomic<int> dropped { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestSynthProcessor)
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynthProcessor();
}
