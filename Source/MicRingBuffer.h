/*
  ==============================================================================

    MicRingBuffer.h
    SmartKeys — búfer circular del micrófono del sistema.

    Un único productor (el hilo de CoreAudio que entrega el micrófono) y un
    único consumidor (el hilo de audio del DAW, dentro de processBlock):

      - Memoria fija (std::array): nunca se asigna ni se libera nada.
      - Sin candados: dos índices atómicos que sólo crecen. La resta sin signo
        da las muestras pendientes aunque el contador dé la vuelta.
      - Si el DAW deja de pedir audio un rato, el búfer se llena: lo que no
        cabe se descarta y el consumidor, al volver, tira lo viejo y sigue con
        lo más reciente (la latencia nunca se acumula).

    16 384 muestras = 341 ms a 48 kHz: cabe un bloque de FL Studio de hasta
    8192 muestras más el bloque del micrófono, sin desbordarse.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace smartkeys
{

template <int Capacity>
class MicRingBuffer
{
    static_assert (Capacity >= 64 && (Capacity & (Capacity - 1)) == 0, "Capacity debe ser potencia de 2");

public:
    static constexpr int capacity = Capacity;

    //==========================================================================
    // PRODUCTOR (hilo del micrófono). Copia un canal de un bloque intercalado:
    // la muestra i es interleaved[i * stride] (stride = canales por trama).
    // Devuelve cuántas muestras cupieron.
    int write (const float* interleaved, int numFrames, int stride = 1) noexcept
    {
        if (interleaved == nullptr || numFrames <= 0 || stride <= 0)
            return 0;

        const uint32_t w = writeIndex.load (std::memory_order_relaxed);
        const uint32_t r = readIndex.load (std::memory_order_acquire);
        const int freeSpace = Capacity - (int) (w - r);
        const int n = std::min (numFrames, std::max (0, freeSpace));

        for (int i = 0; i < n; ++i)
            micRingBuffer[(size_t) ((w + (uint32_t) i) & kMask)] = interleaved[(size_t) i * (size_t) stride];

        writeIndex.store (w + (uint32_t) n, std::memory_order_release);

        if (n < numFrames)
            overflows.fetch_add (1, std::memory_order_release);

        return n;
    }

    //==========================================================================
    // CONSUMIDOR (hilo de audio del DAW)
    int getNumReady() const noexcept
    {
        return (int) (writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_relaxed));
    }

    // Lee hasta 'maxSamples'. Si hay más de 'maxBacklog' pendientes, primero
    // descarta las más antiguas (latencia acotada). Tras un desbordamiento
    // descarta todo lo anterior: sólo se analiza audio continuo y reciente.
    int read (float* dest, int maxSamples, int maxBacklog) noexcept
    {
        if (dest == nullptr || maxSamples <= 0)
            return 0;

        const uint32_t w = writeIndex.load (std::memory_order_acquire);
        uint32_t r = readIndex.load (std::memory_order_relaxed);

        const uint32_t ov = overflows.load (std::memory_order_acquire);
        if (ov != seenOverflows)
        {
            seenOverflows = ov;
            r = w;
        }

        int available = (int) (w - r);
        if (maxBacklog > 0 && available > maxBacklog)
        {
            r = w - (uint32_t) maxBacklog;
            available = maxBacklog;
        }

        const int n = std::min (available, maxSamples);
        for (int i = 0; i < n; ++i)
            dest[i] = micRingBuffer[(size_t) ((r + (uint32_t) i) & kMask)];

        readIndex.store (r + (uint32_t) n, std::memory_order_release);
        return n;
    }

    // Consumidor: tira todo lo pendiente (al cambiar de fuente o de dispositivo)
    void discardAll() noexcept
    {
        seenOverflows = overflows.load (std::memory_order_acquire);
        readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    static constexpr uint32_t kMask = (uint32_t) Capacity - 1;

    std::array<float, (size_t) Capacity> micRingBuffer {};
    std::atomic<uint32_t> writeIndex { 0 }, readIndex { 0 };
    std::atomic<uint32_t> overflows { 0 };
    uint32_t seenOverflows = 0;   // sólo lo toca el consumidor
};

} // namespace smartkeys
