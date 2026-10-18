/*
  ==============================================================================

    VoicePitchTracker.h
    SmartKeys — Controlador vocal en tiempo real (Live Mic-to-MIDI).

    Cadena DSP (todo en el hilo de audio, sin asignaciones de memoria):

      mic -> mono -> DC-block (HPF 50 Hz) -> anti-alias (LPF 3 kHz, 4º orden)
          -> decimación a ~22-24 kHz -> buffer circular de 1024 muestras
          -> cada salto (256 muestras de entrada a 44.1/48 kHz):
               1. Noise gate RMS (gateThresholdDb, por defecto -40 dB)
               2. YIN (65 Hz – 1046 Hz) + interpolación parabólica
               3. Cuantización a la escala activa con histéresis de 35 cents
               4. Contador de estabilidad de 3 frames antes de cambiar de nota
          -> eventos NoteOn / NoteOff con offset de muestra exacto

    Por qué se decima: con 1024 muestras a 48 kHz el periodo de 65 Hz
    (738 muestras) no deja ventana de integración suficiente para YIN. A la
    tasa de análisis (~24 kHz) el mismo buffer de 1024 muestras cubre ~43 ms,
    suficiente para C2, y el coste de CPU se reduce a la cuarta parte.

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include "ScaleChordEngine.h"

namespace smartkeys
{

class VoicePitchTracker
{
public:
    //==========================================================================
    static constexpr int   kBufferSize        = 1024;   // buffer circular (tasa de análisis)
    static constexpr int   kAnalysisHop       = 128;    // = 256 muestras de entrada a 44.1/48 kHz
    static constexpr int   kMaxEvents         = 64;
    static constexpr float kMinFrequencyHz    = 65.0f;  // C2
    static constexpr float kMaxFrequencyHz    = 1046.5f;// C6
    static constexpr float kTargetAnalysisRate = 24000.0f;
    static constexpr float kYinThreshold      = 0.15f;
    static constexpr float kYinUnvoiced       = 0.40f;

    struct Event
    {
        int  sampleOffset = 0;
        bool isNoteOn     = false;
        int  note         = 0;
        int  velocity     = 0;
    };

    //==========================================================================
    VoicePitchTracker() = default;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        decimation = juce::jmax (1, (int) std::lround (sampleRate / (double) kTargetAnalysisRate));
        analysisRate = (float) (sampleRate / (double) decimation);

        hopNative = kAnalysisHop * decimation;

        tauMin = juce::jmax (2, (int) std::floor (analysisRate / kMaxFrequencyHz) - 1);
        tauMax = juce::jmin (kBufferSize / 2 + kBufferSize / 4,
                             (int) std::ceil (analysisRate / kMinFrequencyHz) + 2);
        // Ventana de integración de ~17 ms tomada de las muestras MÁS RECIENTES:
        // reduce la latencia de disparo sin perder el periodo de C2.
        integrationWindow = juce::jmin (kBufferSize - tauMax - 2,
                                        juce::jmax (320, (int) (analysisRate * 0.017f)));

        dcBlock.setHighPass (sampleRate, 50.0, 0.7071);
        lowPassA.setLowPass (sampleRate, 3000.0, 0.5412);  // Butterworth 4º orden
        lowPassB.setLowPass (sampleRate, 3000.0, 1.3066);

        reset();
    }

    void reset() noexcept
    {
        ring.fill (0.0f);
        writePos = 0;
        decimationCounter = 0;
        hopCounter = 0;
        hopSumSquares = 0.0;
        hopSampleCount = 0;
        dcBlock.reset();
        lowPassA.reset();
        lowPassB.reset();

        currentNote = -1;
        pendingNote = -1;
        pendingCount = 0;
        unvoicedCount = 0;
        gateIsOpen = false;
        numEvents = 0;

        inputLevelDb.store (-100.0f);
        detectedFrequency.store (0.0f);
        detectedMidiNote.store (-1.0f);
        activeVoiceNote.store (-1);
        gateOpen.store (false);
    }

    //==========================================================================
    void setGateThresholdDb (float db) noexcept        { gateThresholdDb = juce::jlimit (-90.0f, 0.0f, db); }
    void setHysteresisCents (float cents) noexcept     { hysteresisSemitones = juce::jlimit (0.0f, 49.0f, cents) * 0.01f; }
    void setStabilityFrames (int frames) noexcept      { stabilityFrames = juce::jlimit (1, 16, frames); }
    void setInputGainDb (float db) noexcept            { inputGain = juce::Decibels::decibelsToGain (db); }

    int   getDecimationFactor() const noexcept   { return decimation; }
    float getAnalysisRate() const noexcept       { return analysisRate; }
    int   getHopSizeInInputSamples() const noexcept { return hopNative; }
    int   getCurrentNote() const noexcept        { return currentNote; }

    //==========================================================================
    // Procesa un bloque del bus de micrófono. Devuelve el número de eventos
    // generados (leer con getEvent()). El buffer de entrada NO se modifica aquí;
    // el procesador lo silencia después.
    int process (const float* const* channels, int numChannels, int numSamples,
                 const ScaleChordEngine& engine) noexcept
    {
        numEvents = 0;

        if (channels == nullptr || numChannels <= 0)
            return 0;

        const float channelScale = inputGain / (float) numChannels;

        for (int i = 0; i < numSamples; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                if (channels[ch] != nullptr)
                    x += channels[ch][i];
            x *= channelScale;

            hopSumSquares += (double) x * (double) x;
            ++hopSampleCount;

            // Filtrado a la tasa nativa
            float y = dcBlock.process (x);
            y = lowPassB.process (lowPassA.process (y));

            // Decimación
            if (++decimationCounter >= decimation)
            {
                decimationCounter = 0;
                ring[(size_t) writePos] = y;
                ring[(size_t) (writePos + kBufferSize)] = y;   // espejo -> ventana contigua
                writePos = (writePos + 1) % kBufferSize;

                if (++hopCounter >= kAnalysisHop)
                {
                    hopCounter = 0;
                    analyseFrame (i, engine);
                }
            }
        }

        return numEvents;
    }

    // Fuerza NoteOff de la nota vocal activa (p. ej. al apagar el controlador)
    int forceRelease (int sampleOffset) noexcept
    {
        numEvents = 0;
        if (currentNote >= 0)
            pushEvent (sampleOffset, false, currentNote, 0);
        currentNote = -1;
        pendingNote = -1;
        pendingCount = 0;
        activeVoiceNote.store (-1);
        return numEvents;
    }

    const Event& getEvent (int index) const noexcept   { return events[(size_t) juce::jlimit (0, kMaxEvents - 1, index)]; }

    //==========================================================================
    // Estado para la interfaz (lectura desde el hilo de mensajes)
    std::atomic<float> inputLevelDb      { -100.0f };
    std::atomic<float> detectedFrequency { 0.0f };
    std::atomic<float> detectedMidiNote  { -1.0f };
    std::atomic<int>   activeVoiceNote   { -1 };
    std::atomic<bool>  gateOpen          { false };

    //==========================================================================
    // Análisis YIN público para pruebas: devuelve f0 en Hz (0 si no hay tono)
    // sobre la ventana actual del buffer circular. 'aperiodicity' recibe el
    // valor CMNDF del mínimo elegido.
    float estimatePitch (float& aperiodicity) noexcept
    {
        // Ventana contigua (espejo) que termina en la muestra más reciente
        const int W = integrationWindow;
        const float* x = ring.data() + writePos + (kBufferSize - (W + tauMax));

        // 1) Función diferencia d(tau)
        diff[0] = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            const float* a = x;
            const float* b = x + tau;
            for (int j = 0; j < W; ++j)
            {
                const float d = a[j] - b[j];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        // 2) Diferencia media acumulada normalizada (CMNDF)
        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        // 3) Umbral absoluto + descenso hasta el mínimo local
        int bestTau = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < kYinThreshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                bestTau = tau;
                break;
            }
        }

        // Si nada cruza el umbral, mínimo global (posible tono débil)
        if (bestTau < 0)
        {
            float minVal = 2.0f;
            for (int tau = tauMin; tau < tauMax; ++tau)
            {
                if (cmnd[(size_t) tau] < minVal)
                {
                    minVal = cmnd[(size_t) tau];
                    bestTau = tau;
                }
            }
        }

        if (bestTau <= 0)
        {
            aperiodicity = 1.0f;
            return 0.0f;
        }

        aperiodicity = cmnd[(size_t) bestTau];

        // 4) Interpolación parabólica
        float refinedTau = (float) bestTau;
        if (bestTau > 1 && bestTau < tauMax)
        {
            const float s0 = cmnd[(size_t) (bestTau - 1)];
            const float s1 = cmnd[(size_t) bestTau];
            const float s2 = cmnd[(size_t) (bestTau + 1)];
            const float denom = s0 - 2.0f * s1 + s2;
            if (std::abs (denom) > 1.0e-9f)
                refinedTau += juce::jlimit (-1.0f, 1.0f, 0.5f * (s0 - s2) / denom);
        }

        return refinedTau > 0.0f ? analysisRate / refinedTau : 0.0f;
    }

private:
    //==========================================================================
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() noexcept { z1 = z2 = 0.0f; }

        void setLowPass (double fs, double fc, double q) noexcept
        {
            const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (fc, fs * 0.45) / fs;
            const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (float) (((1.0 - cw) * 0.5) / a0);
            b1 = (float) ((1.0 - cw) / a0);
            b2 = b0;
            a1 = (float) ((-2.0 * cw) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        void setHighPass (double fs, double fc, double q) noexcept
        {
            const double w0 = juce::MathConstants<double>::twoPi * fc / fs;
            const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (float) (((1.0 + cw) * 0.5) / a0);
            b1 = (float) (-(1.0 + cw) / a0);
            b2 = b0;
            a1 = (float) ((-2.0 * cw) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        float process (float x) noexcept   // forma directa II transpuesta
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    //==========================================================================
    void pushEvent (int sampleOffset, bool on, int note, int velocity) noexcept
    {
        if (numEvents >= kMaxEvents)
            return;
        auto& e = events[(size_t) numEvents++];
        e.sampleOffset = sampleOffset;
        e.isNoteOn = on;
        e.note = note;
        e.velocity = velocity;
    }

    void releaseCurrent (int sampleOffset) noexcept
    {
        if (currentNote >= 0)
            pushEvent (sampleOffset, false, currentNote, 0);
        currentNote = -1;
        activeVoiceNote.store (-1);
    }

    void analyseFrame (int sampleOffset, const ScaleChordEngine& engine) noexcept
    {
        // --- 1. Noise gate RMS (con 3 dB de histéresis para evitar parpadeo)
        const double meanSquare = hopSampleCount > 0 ? hopSumSquares / (double) hopSampleCount : 0.0;
        hopSumSquares = 0.0;
        hopSampleCount = 0;

        const float rmsDb = meanSquare > 1.0e-12 ? (float) (10.0 * std::log10 (meanSquare)) : -120.0f;
        inputLevelDb.store (rmsDb);

        const float closeThreshold = gateThresholdDb - 3.0f;
        if (gateIsOpen ? (rmsDb < closeThreshold) : (rmsDb < gateThresholdDb))
        {
            if (gateIsOpen || currentNote >= 0)
                releaseCurrent (sampleOffset);
            gateIsOpen = false;
            gateOpen.store (false);
            pendingNote = -1;
            pendingCount = 0;
            unvoicedCount = 0;
            detectedFrequency.store (0.0f);
            detectedMidiNote.store (-1.0f);
            return;
        }

        gateIsOpen = true;
        gateOpen.store (true);

        // --- 2. YIN
        float aperiodicity = 1.0f;
        const float f0 = estimatePitch (aperiodicity);

        const bool voiced = f0 >= kMinFrequencyHz * 0.97f && f0 <= kMaxFrequencyHz * 1.03f
                            && aperiodicity < kYinUnvoiced;

        if (! voiced)
        {
            detectedFrequency.store (0.0f);
            if (++unvoicedCount >= 8)        // ~45 ms sin tono -> soltar
            {
                releaseCurrent (sampleOffset);
                pendingNote = -1;
                pendingCount = 0;
            }
            return;
        }

        unvoicedCount = 0;
        detectedFrequency.store (f0);

        // --- 3. Cuantización + histéresis de 35 cents
        const float midiFloat = engine.frequencyToMidiNote (f0);
        detectedMidiNote.store (midiFloat);

        int candidate = engine.quantizeFloatNote (midiFloat);

        if (currentNote >= 0 && candidate != currentNote && engine.getIndexInScale (currentNote) >= 0)
        {
            const float boundary = 0.5f * (float) (candidate + currentNote);
            const bool beyond = candidate > currentNote ? (midiFloat > boundary + hysteresisSemitones)
                                                        : (midiFloat < boundary - hysteresisSemitones);
            if (! beyond)
                candidate = currentNote;
        }

        // --- 4. Contador de estabilidad
        if (candidate == pendingNote)
            ++pendingCount;
        else
        {
            pendingNote = candidate;
            pendingCount = 1;
        }

        if (pendingCount >= stabilityFrames && pendingNote != currentNote)
        {
            const float velNorm = juce::jlimit (0.0f, 1.0f, (rmsDb - gateThresholdDb) / 30.0f);
            const int velocity = juce::jlimit (1, 127, (int) std::lround (50.0f + velNorm * 70.0f));

            releaseCurrent (sampleOffset);
            currentNote = pendingNote;
            pushEvent (sampleOffset, true, currentNote, velocity);
            activeVoiceNote.store (currentNote);
        }
    }

    //==========================================================================
    double sampleRate = 44100.0;
    int    decimation = 2;
    float  analysisRate = 22050.0f;
    int    hopNative = 256;
    int    tauMin = 20, tauMax = 342, integrationWindow = 680;

    float gateThresholdDb = -40.0f;
    float hysteresisSemitones = 0.35f;
    int   stabilityFrames = 3;
    float inputGain = 1.0f;

    Biquad dcBlock, lowPassA, lowPassB;

    std::array<float, (size_t) kBufferSize * 2> ring {};
    std::array<float, (size_t) kBufferSize> diff {};
    std::array<float, (size_t) kBufferSize> cmnd {};
    int writePos = 0;
    int decimationCounter = 0;
    int hopCounter = 0;
    double hopSumSquares = 0.0;
    int hopSampleCount = 0;

    int  currentNote = -1;
    int  pendingNote = -1;
    int  pendingCount = 0;
    int  unvoicedCount = 0;
    bool gateIsOpen = false;

    std::array<Event, (size_t) kMaxEvents> events {};
    int numEvents = 0;
};

} // namespace smartkeys
