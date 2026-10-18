/*
  ==============================================================================

    HardwareMicInput.h
    SmartKeys — Vía B del controlador vocal: lectura DIRECTA del micrófono /
    interfaz de audio desde el propio plugin, sin depender del ruteo del DAW.

    - Abre el dispositivo elegido SOLO COMO ENTRADA (nunca toca la salida del
      DAW). En macOS, CoreAudio permite que FL Studio / Ableton y SmartKeys
      lean la misma interfaz (p. ej. Volt 1) al mismo tiempo.
    - El hilo del dispositivo mezcla a mono y escribe en un FIFO lock-free; el
      hilo de audio del plugin lo vacía en cada processBlock (cero bloqueos).
    - Si el host deja de llamar a processBlock, el FIFO descarta lo más antiguo
      para que la latencia nunca se acumule.

    Permiso: el micrófono lo autoriza macOS para la aplicación anfitriona
    (FL Studio, Ableton Live). Si el DAW no tiene permiso, el dispositivo
    entrega silencio; la interfaz de SmartKeys lo indica.

  ==============================================================================
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>

namespace smartkeys
{

//==============================================================================
// FIFO mono lock-free: 1 productor (hilo del dispositivo) y 1 consumidor (hilo de audio)
class MicSampleFifo
{
public:
    explicit MicSampleFifo (int capacity = 1 << 15)
        : fifo (capacity), buffer ((size_t) capacity, 0.0f) {}

    // Productor: escribe lo que quepa y devuelve cuántas muestras escribió
    int push (const float* samples, int numSamples) noexcept
    {
        const auto scope = fifo.write (numSamples);
        if (scope.blockSize1 > 0)
            std::copy (samples, samples + scope.blockSize1, buffer.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (samples + scope.blockSize1, samples + scope.blockSize1 + scope.blockSize2,
                       buffer.begin() + scope.startIndex2);
        return scope.blockSize1 + scope.blockSize2;
    }

    // Consumidor: lee hasta 'maxSamples'. Si hay más de 'maxLatencySamples'
    // acumuladas, descarta las más antiguas para mantener la latencia acotada.
    int pull (float* dest, int maxSamples, int maxLatencySamples) noexcept
    {
        int ready = fifo.getNumReady();
        if (ready > maxLatencySamples)
        {
            const int drop = ready - maxLatencySamples / 2;
            fifo.read (drop);   // el ScopedRead temporal confirma la lectura al destruirse
            ready -= drop;
        }

        const int n = juce::jmin (ready, maxSamples);
        const auto scope = fifo.read (n);
        if (scope.blockSize1 > 0)
            std::copy (buffer.begin() + scope.startIndex1, buffer.begin() + scope.startIndex1 + scope.blockSize1, dest);
        if (scope.blockSize2 > 0)
            std::copy (buffer.begin() + scope.startIndex2, buffer.begin() + scope.startIndex2 + scope.blockSize2,
                       dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    // Consumidor: vacía todo (al cambiar de dispositivo)
    void discardAll() noexcept         { fifo.read (fifo.getNumReady()); }
    int getNumReady() const noexcept   { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> buffer;
};

//==============================================================================
class HardwareMicInput : private juce::AudioIODeviceCallback
{
public:
    enum class ChannelMode : int { First = 0, Second = 1, Mix = 2 };

    HardwareMicInput() = default;
    ~HardwareMicInput() override { close(); }

    //==========================================================================
    // Hilo de mensajes
    juce::StringArray getInputDeviceNames()
    {
        if (auto* type = getDeviceType())
        {
            type->scanForDevices();
            return type->getDeviceNames (true);
        }
        return {};
    }

    juce::String getDefaultInputDeviceName()
    {
        if (auto* type = getDeviceType())
        {
            type->scanForDevices();
            const auto names = type->getDeviceNames (true);
            const int index = type->getDefaultDeviceIndex (true);
            if (juce::isPositiveAndBelow (index, names.size()))
                return names[index];
            if (! names.isEmpty())
                return names[0];
        }
        return {};
    }

    juce::String getDriverName()
    {
        auto* type = getDeviceType();
        return type != nullptr ? type->getTypeName() : juce::String();
    }

    // Abre 'deviceName' sólo como entrada. Devuelve "" si todo fue bien o el error.
    juce::String open (const juce::String& deviceName, ChannelMode mode)
    {
        close();
        channelMode.store ((int) mode);

        auto* type = getDeviceType();
        if (type == nullptr)
            return "No hay drivers de audio disponibles en este sistema.";

        type->scanForDevices();
        if (! type->getDeviceNames (true).contains (deviceName))
            return juce::String::fromUTF8 ("No se encontró la entrada \"") + deviceName + "\".";

        std::unique_ptr<juce::AudioIODevice> newDevice (type->createDevice ({}, deviceName));
        if (newDevice == nullptr)
            return "No se pudo crear el dispositivo \"" + deviceName + "\".";

        // Frecuencia: 48 kHz o 44.1 kHz si existen; si no, la primera disponible
        const auto rates = newDevice->getAvailableSampleRates();
        double rate = rates.isEmpty() ? 48000.0 : rates[0];
        for (double preferred : { 48000.0, 44100.0 })
            if (rates.contains (preferred)) { rate = preferred; break; }

        const int bufferSize = juce::jlimit (64, 2048, newDevice->getDefaultBufferSize());

        juce::BigInteger inputs;
        inputs.setRange (0, 2, true);   // canales 1 y 2 (si sólo hay 1, el driver usa ese)

        const auto error = newDevice->open (inputs, {}, rate, bufferSize);
        if (error.isNotEmpty())
            return error;

        if (newDevice->getActiveInputChannels().countNumberOfSetBits() == 0)
        {
            newDevice->close();
            return "La entrada \"" + deviceName + "\" no tiene canales activos.";
        }

        device = std::move (newDevice);
        openDeviceName = deviceName;
        device->start (this);
        deviceOpen.store (true);
        return {};
    }

    void close()
    {
        if (device != nullptr)
        {
            device->stop();
            device->close();
            device.reset();
        }
        deviceOpen.store (false);
        openDeviceName.clear();
        peakLevel.store (0.0f);
    }

    bool isOpen() const noexcept                   { return deviceOpen.load(); }
    juce::String getOpenDeviceName() const          { return openDeviceName; }
    double getDeviceSampleRate() const noexcept     { return deviceRate.load(); }
    void setChannelMode (ChannelMode m) noexcept    { channelMode.store ((int) m); }
    ChannelMode getChannelMode() const noexcept     { return (ChannelMode) channelMode.load(); }

    // Milisegundos desde que llegó la última muestra distinta de cero (diagnóstico de permiso)
    double getMillisecondsSinceSignal() const noexcept
    {
        const double last = lastSignalTimeMs.load();
        return last <= 0.0 ? 1.0e9 : juce::Time::getMillisecondCounterHiRes() - last;
    }

    double getMillisecondsSinceStart() const noexcept
    {
        const double start = startTimeMs.load();
        return start <= 0.0 ? 0.0 : juce::Time::getMillisecondCounterHiRes() - start;
    }

    //==========================================================================
    // Hilo de audio del plugin
    int pullSamples (float* dest, int maxSamples, int maxLatencySamples) noexcept
    {
        if (flushRequested.exchange (false))
            fifo.discardAll();
        return fifo.pull (dest, maxSamples, maxLatencySamples);
    }

    // true una vez cada vez que el dispositivo (re)arranca -> preparar el detector a su frecuencia
    bool consumeRestartFlag() noexcept     { return restartFlag.exchange (false); }

    std::atomic<float> peakLevel { 0.0f };

private:
    //==========================================================================
    juce::AudioIODeviceType* getDeviceType()
    {
        if (types.isEmpty())
        {
            juce::AudioDeviceManager manager;   // sólo para obtener los tipos de driver de la plataforma
            manager.createAudioDeviceTypes (types);
        }
        return types.isEmpty() ? nullptr : types.getFirst();   // CoreAudio / Windows Audio / ALSA
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        deviceRate.store (d->getCurrentSampleRate());
        monoScratch.assign ((size_t) juce::jmax (4096, d->getCurrentBufferSizeSamples() * 2), 0.0f);
        startTimeMs.store (juce::Time::getMillisecondCounterHiRes());
        lastSignalTimeMs.store (0.0);
        flushRequested.store (true);
        restartFlag.store (true);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        if (numInputChannels <= 0 || monoScratch.empty())
            return;

        const int mode = channelMode.load();
        float peak = 0.0f;
        int offset = 0;

        while (offset < numSamples)
        {
            const int chunk = juce::jmin (numSamples - offset, (int) monoScratch.size());
            float* mono = monoScratch.data();

            if (mode == (int) ChannelMode::Mix && numInputChannels > 1)
            {
                juce::FloatVectorOperations::clear (mono, chunk);
                int used = 0;
                for (int ch = 0; ch < numInputChannels; ++ch)
                {
                    if (inputChannelData[ch] == nullptr)
                        continue;
                    juce::FloatVectorOperations::add (mono, inputChannelData[ch] + offset, chunk);
                    ++used;
                }
                if (used > 1)
                    juce::FloatVectorOperations::multiply (mono, 1.0f / (float) used, chunk);
            }
            else
            {
                const int ch = (mode == (int) ChannelMode::Second && numInputChannels > 1) ? 1 : 0;
                if (inputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::copy (mono, inputChannelData[ch] + offset, chunk);
                else
                    juce::FloatVectorOperations::clear (mono, chunk);
            }

            const auto range = juce::FloatVectorOperations::findMinAndMax (mono, chunk);
            peak = juce::jmax (peak, std::abs (range.getStart()), std::abs (range.getEnd()));

            fifo.push (mono, chunk);
            offset += chunk;
        }

        peakLevel.store (peak);
        if (peak > 0.0f)
            lastSignalTimeMs.store (juce::Time::getMillisecondCounterHiRes());
    }

    //==========================================================================
    juce::OwnedArray<juce::AudioIODeviceType> types;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String openDeviceName;

    MicSampleFifo fifo;
    std::vector<float> monoScratch;

    std::atomic<int>    channelMode { (int) ChannelMode::Mix };
    std::atomic<bool>   deviceOpen { false }, restartFlag { false }, flushRequested { false };
    std::atomic<double> deviceRate { 48000.0 };
    std::atomic<double> startTimeMs { 0.0 }, lastSignalTimeMs { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareMicInput)
};

} // namespace smartkeys
