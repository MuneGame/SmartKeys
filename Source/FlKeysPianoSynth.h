/*
  ==============================================================================

    FlKeysPianoSynth.h
    SmartKeys — Motor de piano interno ligero (estilo "FL Keys"), 100 % código.

    - 32 voces polifónicas (juce::Synthesiser), sin samples externos: un
      acorde COMPLEX de 7 notas + la cola de release del acorde anterior +
      pedal de sustain caben sin robar voces (mínimo exigido: 24).
    - Síntesis aditiva por tabla de ondas: fundamental + 8 armónicos con
      inarmonicidad ligera dependiente del registro (f_n = n·f0·sqrt(1 + B·n²)),
      decaimiento independiente por parcial y filtro de "posición del martillo".
    - Transitorio de martillo: ruido filtrado pasa-banda, sólo los primeros 8 ms.
    - Filtro dinámico por velocidad: juce::dsp::StateVariableTPTFilter paso-bajo
      cuya frecuencia de corte se abre con la velocidad y se cierra lentamente.
    - ADSR de piano: A = 0.003 s, D = 1.4 s, S = 0.15, R = 0.25 s.

    Nota de tiempo real: juce::Synthesiser usa internamente un CriticalSection
    que sólo toma el hilo de audio (nunca hay contención porque el plugin no
    modifica voces desde otros hilos), así que nunca bloquea.

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>

namespace smartkeys
{

//==============================================================================
// Tabla de seno compartida (se construye una sola vez, fuera del hilo de audio)
class SineTable
{
public:
    static constexpr int kSize = 2048;

    static const SineTable& get()
    {
        static const SineTable instance;
        return instance;
    }

    // phase en [0, 1)
    inline float lookup (float phase) const noexcept
    {
        const float pos = phase * (float) kSize;
        const int i = (int) pos;
        const float frac = pos - (float) i;
        const float a = table[(size_t) (i & (kSize - 1))];
        const float b = table[(size_t) ((i + 1) & (kSize - 1))];
        return a + frac * (b - a);
    }

private:
    SineTable()
    {
        for (int i = 0; i < kSize; ++i)
            table[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * (double) i / (double) kSize);
    }

    std::array<float, (size_t) kSize> table {};
};

//==============================================================================
class PianoSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
class PianoVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int kNumPartials = 9;   // fundamental + 8 armónicos

    explicit PianoVoice (const std::array<float, 128>& frequencyTable)
        : noteFrequencies (frequencyTable)
    {
        SineTable::get();
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter.setResonance (0.62f);
    }

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        juce::dsp::ProcessSpec spec { newSampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 1 };
        filter.prepare (spec);
        filter.reset();

        juce::ADSR::Parameters p;
        p.attack  = 0.003f;
        p.decay   = 1.4f;
        p.sustain = 0.15f;
        p.release = 0.25f;
        adsr.setSampleRate (newSampleRate);
        adsr.setParameters (p);
        adsr.reset();

        hammerFilter.reset();
    }

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<PianoSound*> (s) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        const float vel = juce::jlimit (0.0f, 1.0f, velocity);
        noteVelocity = vel;
        pitchBendFactor = pitchWheelToFactor (currentPitchWheelPosition);
        baseFrequency = noteFrequencies[(size_t) juce::jlimit (0, 127, midiNoteNumber)];

        const float noteF = (float) midiNoteNumber;

        // Inarmonicidad ligera por registro (graves ~0.00015, agudos ~0.0008)
        const float B = 0.00015f * std::pow (2.0f, (noteF - 21.0f) / 36.0f);

        // Tiempo de decaimiento base del parcial fundamental (graves largos, agudos cortos)
        const float baseDecaySeconds = 7.0f * std::pow (2.0f, -(noteF - 21.0f) / 24.0f);

        // Brillo: los parciales altos ganan peso con la velocidad
        const float tilt = 1.45f - 0.55f * vel;

        float amplitudeSum = 0.0f;
        for (int k = 0; k < kNumPartials; ++k)
        {
            const float n = (float) (k + 1);
            partialRatio[(size_t) k] = n * std::sqrt (1.0f + B * n * n);

            // Filtro de posición del martillo (golpe a ~1/8.5 de la cuerda)
            const float strike = std::abs (std::sin (juce::MathConstants<float>::pi * n / 8.5f));
            float amp = std::pow (n, -tilt) * (0.35f + 0.65f * strike);

            // Los agudos tienen menos armónicos audibles
            if (noteF > 84.0f)
                amp *= std::pow (0.7f, (n - 1.0f) * (noteF - 84.0f) / 24.0f);

            partialAmp[(size_t) k] = amp;
            amplitudeSum += amp;

            const float decaySeconds = juce::jmax (0.05f, baseDecaySeconds / (1.0f + 0.55f * (n - 1.0f)));
            partialDecayMul[(size_t) k] = (float) std::exp (-1.0 / ((double) decaySeconds * sampleRate));
            partialEnv[(size_t) k] = 1.0f;
            partialPhase[(size_t) k] = 0.0f;
        }

        const float norm = amplitudeSum > 0.0f ? 1.0f / amplitudeSum : 1.0f;
        for (auto& a : partialAmp)
            a *= norm;

        updatePhaseIncrements();

        // Nivel de la voz (curva de velocidad suave)
        voiceGain = 0.22f * (0.12f + 0.88f * std::pow (vel, 1.6f));

        // Panorama según registro (graves izquierda, agudos derecha)
        const float pan = juce::jmap (juce::jlimit (21.0f, 108.0f, noteF), 21.0f, 108.0f, -0.35f, 0.35f);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        gainL = std::cos (angle);
        gainR = std::sin (angle);

        // Filtro dinámico por velocidad + seguimiento de teclado
        cutoffStart = juce::jlimit (300.0f, 18000.0f,
                                    (700.0f + 11000.0f * vel * vel) * std::pow (2.0f, (noteF - 60.0f) / 36.0f));
        cutoffEnd = juce::jmax (250.0f, cutoffStart * 0.45f);
        cutoffCurrent = cutoffStart;
        cutoffGlideMul = (float) std::exp (-1.0 / (0.8 * sampleRate));   // cierre en ~0.8 s
        filter.reset();
        filter.setCutoffFrequency (juce::jmin (cutoffCurrent, (float) sampleRate * 0.45f));
        filterUpdateCounter = 0;

        // Transitorio de martillo (8 ms)
        hammerSamplesLeft = (int) std::lround (0.008 * sampleRate);
        hammerEnv = 1.0f;
        hammerDecayMul = (float) std::exp (-1.0 / (0.0018 * sampleRate));
        hammerLevel = 0.35f * vel;
        hammerFilter.setBandPass (sampleRate, 1800.0 + 2600.0 * (double) vel, 1.1);
        hammerFilter.reset();
        noiseState = 0x9E3779B9u ^ ((uint32_t) midiNoteNumber * 2654435761u);

        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newPitchWheelValue) override
    {
        pitchBendFactor = pitchWheelToFactor (newPitchWheelValue);
        updatePhaseIncrements();
    }

    void controllerMoved (int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const auto& sine = SineTable::get();
        const int numChannels = output.getNumChannels();
        float* left  = output.getWritePointer (0);
        float* right = numChannels > 1 ? output.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            // --- Aditiva
            float s = 0.0f;
            for (int k = 0; k < kNumPartials; ++k)
            {
                auto& ph = partialPhase[(size_t) k];
                s += sine.lookup (ph) * partialAmp[(size_t) k] * partialEnv[(size_t) k];
                ph += partialInc[(size_t) k];
                if (ph >= 1.0f) ph -= 1.0f;
                partialEnv[(size_t) k] *= partialDecayMul[(size_t) k];
            }

            // --- Martillo (ruido filtrado, sólo 8 ms)
            if (hammerSamplesLeft > 0)
            {
                noiseState = noiseState * 1664525u + 1013904223u;
                const float noise = (float) (int32_t) noiseState * (1.0f / 2147483648.0f);
                s += hammerFilter.process (noise) * hammerEnv * hammerLevel;
                hammerEnv *= hammerDecayMul;
                --hammerSamplesLeft;
            }

            // --- Filtro dinámico (actualización del corte cada 32 muestras)
            if (--filterUpdateCounter <= 0)
            {
                filterUpdateCounter = 32;
                cutoffCurrent = cutoffEnd + (cutoffCurrent - cutoffEnd) * std::pow (cutoffGlideMul, 32.0f);
                filter.setCutoffFrequency (juce::jmin (cutoffCurrent, (float) sampleRate * 0.45f));
            }
            s = filter.processSample (0, s);

            // --- Envolvente ADSR
            const float out = s * adsr.getNextSample() * voiceGain;

            left[startSample + i] += out * gainL;
            if (right != nullptr)
                right[startSample + i] += out * gainR;

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    //==========================================================================
    struct BandPass
    {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0f; }
        void setBandPass (double fs, double fc, double q) noexcept
        {
            const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (fc, fs * 0.45) / fs;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (float) (alpha / a0);
            b2 = -b0;
            a1 = (float) (-2.0 * std::cos (w0) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = -a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static float pitchWheelToFactor (int wheel) noexcept
    {
        const float semis = 2.0f * ((float) wheel - 8192.0f) / 8192.0f;   // ±2 semitonos
        return std::pow (2.0f, semis / 12.0f);
    }

    void updatePhaseIncrements() noexcept
    {
        const float nyquistGuard = (float) sampleRate * 0.45f;
        const float f0 = baseFrequency * pitchBendFactor;
        for (int k = 0; k < kNumPartials; ++k)
        {
            const float f = f0 * partialRatio[(size_t) k];
            if (f < nyquistGuard)
                partialInc[(size_t) k] = f / (float) sampleRate;
            else
            {
                partialInc[(size_t) k] = 0.0f;
                partialAmp[(size_t) k] = 0.0f;   // anti-aliasing
            }
        }
    }

    //==========================================================================
    const std::array<float, 128>& noteFrequencies;
    double sampleRate = 44100.0;

    std::array<float, kNumPartials> partialRatio {};
    std::array<float, kNumPartials> partialAmp {};
    std::array<float, kNumPartials> partialEnv {};
    std::array<float, kNumPartials> partialDecayMul {};
    std::array<float, kNumPartials> partialPhase {};
    std::array<float, kNumPartials> partialInc {};

    float baseFrequency = 440.0f;
    float pitchBendFactor = 1.0f;
    float noteVelocity = 0.0f;
    float voiceGain = 0.0f;
    float gainL = 0.707f, gainR = 0.707f;

    juce::dsp::StateVariableTPTFilter<float> filter;
    float cutoffStart = 8000.0f, cutoffEnd = 3000.0f, cutoffCurrent = 8000.0f, cutoffGlideMul = 0.9999f;
    int filterUpdateCounter = 0;

    BandPass hammerFilter;
    int hammerSamplesLeft = 0;
    float hammerEnv = 0.0f, hammerDecayMul = 0.99f, hammerLevel = 0.0f;
    uint32_t noiseState = 1;

    juce::ADSR adsr;
};

//==============================================================================
//==============================================================================
// juce::Synthesiser con robo de voces SIN memoria dinámica: el original crea un
// Array temporal en cada robo (findVoiceToSteal), es decir, pide memoria en el
// hilo de audio. Mismas reglas: la nota repetida primero, luego la voz más
// antigua ya soltada, la más antigua sin dedo encima y la más antigua no
// protegida; la nota más grave y la más aguda que siguen pulsadas se protegen.
class StealSafeSynthesiser : public juce::Synthesiser
{
protected:
    juce::SynthesiserVoice* findVoiceToSteal (juce::SynthesiserSound* sound, int, int midiNoteNumber) const override
    {
        juce::SynthesiserVoice* low = nullptr;   // nota más grave que sigue pulsada (no en release)
        juce::SynthesiserVoice* top = nullptr;   // nota más aguda que sigue pulsada

        for (auto* v : voices)
        {
            if (v->canPlaySound (sound) && ! v->isPlayingButReleased())
            {
                const int note = v->getCurrentlyPlayingNote();
                if (low == nullptr || note < low->getCurrentlyPlayingNote())  low = v;
                if (top == nullptr || note > top->getCurrentlyPlayingNote())  top = v;
            }
        }
        if (top == low)
            top = nullptr;

        auto oldestWhere = [&] (auto&& predicate) -> juce::SynthesiserVoice*
        {
            juce::SynthesiserVoice* best = nullptr;
            for (auto* v : voices)
                if (v->canPlaySound (sound) && predicate (v) && (best == nullptr || v->wasStartedBefore (*best)))
                    best = v;
            return best;
        };

        if (auto* v = oldestWhere ([&] (auto* x) { return x->getCurrentlyPlayingNote() == midiNoteNumber; }))  return v;
        if (auto* v = oldestWhere ([&] (auto* x) { return x != low && x != top && x->isPlayingButReleased(); }))  return v;
        if (auto* v = oldestWhere ([&] (auto* x) { return x != low && x != top && ! x->isKeyDown(); }))  return v;
        if (auto* v = oldestWhere ([&] (auto* x) { return x != low && x != top; }))  return v;
        return top != nullptr ? top : low;
    }
};

//==============================================================================
class FlKeysPianoSynth
{
public:
    static constexpr int kNumVoices = 32;
    static_assert (kNumVoices >= 24, "COMPLEX CHORDS necesita al menos 24 voces");

    FlKeysPianoSynth()
    {
        setReferencePitch (440.0f);
        for (int i = 0; i < kNumVoices; ++i)
            synth.addVoice (new PianoVoice (noteFrequencies));
        synth.addSound (new PianoSound());
        synth.setNoteStealingEnabled (true);
    }

    void prepare (double sampleRate, int maxBlockSize)
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<PianoVoice*> (synth.getVoice (i)))
                v->prepare (sampleRate, maxBlockSize);
    }

    // Tabla de frecuencias (afinación 12-TET + desviaciones microtonales opcionales).
    // Sólo se llama desde el hilo de audio, antes de renderizar.
    void setNoteFrequency (int midiNote, float hz) noexcept
    {
        if (midiNote >= 0 && midiNote < 128)
            noteFrequencies[(size_t) midiNote] = hz;
    }

    void setReferencePitch (float referenceHz) noexcept
    {
        for (int n = 0; n < 128; ++n)
            noteFrequencies[(size_t) n] = referenceHz * std::pow (2.0f, ((float) n - 69.0f) / 12.0f);
    }

    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int startSample, int numSamples)
    {
        synth.renderNextBlock (buffer, midi, startSample, numSamples);
    }

    void allNotesOff() { synth.allNotesOff (0, false); }

    int getNumActiveVoices() const
    {
        int n = 0;
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = synth.getVoice (i); v != nullptr && v->isVoiceActive())
                ++n;
        return n;
    }

    juce::Synthesiser& getSynthesiser() noexcept { return synth; }

private:
    std::array<float, 128> noteFrequencies {};
    StealSafeSynthesiser synth;
};

} // namespace smartkeys
