/*
  ==============================================================================

    SystemMicInput.h
    SmartKeys — lectura DIRECTA del micrófono del sistema, sin sidechain.

    En FL Studio un instrumento del Channel Rack no recibe el micrófono por su
    entrada a menos que se haga un ruteo de sidechain. Cuando la entrada del
    plugin llega en silencio digital y MIC VOICE está en ON, SmartKeys abre la
    ENTRADA predeterminada de macOS (canal 1) y la lee él mismo:

      - Sólo entrada: un IOProc de CoreAudio con 0 canales de salida. Nunca
        cambia la frecuencia de muestreo ni el tamaño de búfer del dispositivo
        y nunca detiene ni reinicia el audio de FL Studio (CoreAudio admite
        varios clientes sobre el mismo dispositivo).
      - El hilo de CoreAudio sólo copia muestras a un búfer circular fijo y
        lock-free (MicRingBuffer); el processBlock del DAW las lee sin esperar.
      - Permiso de macOS: se pide con el aviso oficial (lo concede el DAW, p.
        ej. "FL Studio"). Si el anfitrión no declara el uso del micrófono en su
        Info.plist, no se toca el micrófono (macOS cerraría el programa).
      - Una entrada Bluetooth (AirPods) no se abre: macOS cambiaría los
        auriculares a modo manos libres y alteraría la salida de audio del DAW.
        Se usa el micrófono de la Mac si existe.
      - Si el usuario cambia la entrada en Ajustes del Sistema, desconecta la
        interfaz o el dispositivo deja de entregar audio, se reabre solo.

    Todo el control ocurre en el hilo de mensajes (update() desde un Timer);
    el hilo de audio sólo usa pull() y atómicos.

    Windows: no hay lectura directa (la voz llega por la entrada del plugin).
    Linux: micrófono simulado para las pruebas (SMARTKEYS_FAKE_MIC).

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include "MicRingBuffer.h"

namespace smartkeys
{

enum class MicDeviceState : int
{
    Off = 0,            // no hace falta (MIC VOICE apagado o la voz ya llega por el DAW)
    Running,            // leyendo el micrófono del sistema
    WaitingPermission,  // macOS está preguntando si el DAW puede usar el micrófono
    PermissionDenied,   // macOS bloquea el micrófono para el DAW
    HostNotAllowed,     // el programa anfitrión no declara el uso del micrófono
    NoDevice,           // no hay ninguna entrada de audio
    BluetoothOnly,      // la única entrada es Bluetooth: no se abre
    Unsupported,        // sin lectura directa en esta plataforma (Windows)
    Error               // no se pudo abrir: se reintenta cada pocos segundos
};

class SystemMicInput
{
public:
    static constexpr int kRingCapacity = 16384;
    using Ring = MicRingBuffer<kRingCapacity>;

    enum class Permission { Granted, NotDetermined, Denied };

    struct DeviceChoice
    {
        MicDeviceState state = MicDeviceState::NoDevice;   // Running = hay una entrada utilizable
        uint32_t id = 0;
        juce::String name;
        bool replacedBluetooth = false;   // la entrada predeterminada era Bluetooth: se usa la de la Mac
    };

    //==========================================================================
    // Lo que depende del sistema (CoreAudio en macOS, simulado en Linux)
    class Backend
    {
    public:
        virtual ~Backend() = default;
        virtual bool isSupported() = 0;
        virtual bool hostAllowsMicrophone() = 0;
        virtual Permission getPermission() = 0;
        virtual void requestPermission() = 0;
        virtual DeviceChoice chooseDevice() = 0;
        virtual juce::String start (const DeviceChoice&, Ring&) = 0;   // "" = abierto
        virtual void stop() = 0;
        virtual bool isAlive() = 0;
        virtual double getSampleRate() = 0;
        virtual uint64_t getFramesReceived() const = 0;
    };

    static std::unique_ptr<Backend> createPlatformBackend();

    explicit SystemMicInput (std::unique_ptr<Backend> backendToUse = createPlatformBackend());
    ~SystemMicInput();

    //==========================================================================
    // Hilo de mensajes. update() se llama ~4 veces por segundo: abre, vigila,
    // reabre o cierra el micrófono según 'wanted'.
    void update (bool wanted);
    void shutdown();

    MicDeviceState getState() const noexcept   { return (MicDeviceState) state.load (std::memory_order_acquire); }
    juce::String getDeviceName() const          { return deviceName; }
    bool isUsingBuiltInInsteadOfBluetooth() const noexcept { return bluetoothReplaced; }
    juce::String getLastError() const           { return lastError; }

    //==========================================================================
    // Hilo de audio (único consumidor del búfer)
    bool isRunning() const noexcept             { return running.load (std::memory_order_acquire); }
    double getSampleRate() const noexcept       { return sampleRate.load (std::memory_order_acquire); }
    uint32_t getGeneration() const noexcept     { return generation.load (std::memory_order_acquire); }
    int pull (float* dest, int maxSamples, int maxBacklog) noexcept { return ring.read (dest, maxSamples, maxBacklog); }
    void discardAll() noexcept                  { ring.discardAll(); }

    static constexpr int kStallTicks = 8;            // 8 x 250 ms sin audio -> se reabre
    static constexpr double kRetryDelayMs = 3000.0;

private:
    void stopStream();
    void setState (MicDeviceState s) noexcept   { state.store ((int) s, std::memory_order_release); }

    Ring ring;                          // declarado ANTES que el backend: se destruye después
    std::unique_ptr<Backend> backend;

    std::atomic<bool>     running { false };
    std::atomic<double>   sampleRate { 48000.0 };
    std::atomic<uint32_t> generation { 0 };
    std::atomic<int>      state { (int) MicDeviceState::Off };

    // Sólo hilo de mensajes
    bool streamOpen = false, permissionRequested = false, bluetoothReplaced = false;
    uint32_t openDeviceId = 0;
    juce::String deviceName, lastError;
    uint64_t lastFrames = 0;
    int stalledTicks = 0;
    double retryAtMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE (SystemMicInput)
};

} // namespace smartkeys
