/*
  ==============================================================================

    SystemMicInput.cpp
    SmartKeys — lectura directa del micrófono del sistema (ver SystemMicInput.h).

  ==============================================================================
*/

#include "SystemMicInput.h"

#include <cstring>
#include <vector>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
 #include <CoreFoundation/CoreFoundation.h>
 #include "MicPermission.h"
#elif JUCE_LINUX
 #include <chrono>
 #include <cmath>
 #include <thread>
#endif

namespace smartkeys
{

//==============================================================================
// CONTROL (hilo de mensajes)
//==============================================================================
SystemMicInput::SystemMicInput (std::unique_ptr<Backend> backendToUse)
    : backend (std::move (backendToUse))
{
}

SystemMicInput::~SystemMicInput()
{
    shutdown();
    backend.reset();   // el dispositivo ya está detenido: el búfer puede destruirse
}

void SystemMicInput::shutdown()
{
    const juce::ScopedLock sl (controlLock);
    stopStream();
    setState (MicDeviceState::Off);
}

void SystemMicInput::stopStream()
{
    if (! streamOpen)
        return;

    // Primero el hilo de audio deja de leer; después se detiene el IOProc
    // (AudioDeviceStop espera a que termine la llamada en curso).
    running.store (false, std::memory_order_release);
    backend->stop();
    streamOpen = false;
    openDeviceId = 0;
    stalledTicks = 0;
}

void SystemMicInput::update (bool wanted)
{
    const juce::ScopedLock sl (controlLock);

    if (backend == nullptr || ! backend->isSupported())
    {
        stopStream();
        setState (MicDeviceState::Unsupported);
        return;
    }

    if (! wanted)
    {
        stopStream();
        setState (MicDeviceState::Off);
        return;
    }

    // 1) El programa anfitrión debe declarar el uso del micrófono
    if (! backend->hostAllowsMicrophone())
    {
        stopStream();
        setState (MicDeviceState::HostNotAllowed);
        return;
    }

    // 2) Permiso de macOS (el aviso aparece una sola vez, con el nombre del DAW).
    //    Con el micrófono ya abierto no se vuelve a preguntar en cada vuelta.
    switch (streamOpen ? Permission::Granted : backend->getPermission())
    {
        case Permission::NotDetermined:
            stopStream();
            if (! permissionRequested)
            {
                permissionRequested = true;
                backend->requestPermission();
            }
            setState (MicDeviceState::WaitingPermission);
            return;

        case Permission::Denied:
            stopStream();
            setState (MicDeviceState::PermissionDenied);
            return;

        case Permission::Granted:
            break;
    }

    // 3) La entrada activa de macOS (evitando Bluetooth)
    const auto choice = backend->chooseDevice();
    if (choice.state != MicDeviceState::Running)
    {
        stopStream();
        deviceName = choice.name;
        setState (choice.state);
        return;
    }

    // 4) Ya abierta: vigilar que siga viva y entregando audio
    if (streamOpen)
    {
        if (choice.id == openDeviceId && backend->isAlive())
        {
            const double rate = backend->getSampleRate();
            if (rate > 1000.0)
                sampleRate.store (rate, std::memory_order_release);

            const auto frames = backend->getFramesReceived();
            if (frames != lastFrames)
            {
                lastFrames = frames;
                stalledTicks = 0;
            }
            else
            {
                ++stalledTicks;
            }

            if (stalledTicks < kStallTicks)
            {
                setState (MicDeviceState::Running);
                return;
            }
        }

        // Otra entrada elegida, dispositivo desconectado o sin audio: reabrir
        stopStream();
        retryAtMs = 0.0;
    }

    // 5) Abrir (con espera entre reintentos si falló)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now < retryAtMs)
    {
        setState (MicDeviceState::Error);
        return;
    }

    const auto error = backend->start (choice, ring);
    if (error.isNotEmpty())
    {
        lastError = error;
        deviceName = choice.name;
        retryAtMs = now + kRetryDelayMs;
        setState (MicDeviceState::Error);
        return;
    }

    streamOpen = true;
    openDeviceId = choice.id;
    deviceName = choice.name;
    bluetoothReplaced = choice.replacedBluetooth;
    lastError.clear();
    lastFrames = backend->getFramesReceived();
    stalledTicks = 0;

    const double rate = backend->getSampleRate();
    sampleRate.store (rate > 1000.0 ? rate : 48000.0, std::memory_order_release);
    generation.fetch_add (1, std::memory_order_acq_rel);   // el hilo de audio descarta lo anterior
    running.store (true, std::memory_order_release);
    setState (MicDeviceState::Running);
}

//==============================================================================
// macOS: CoreAudio (HAL) — un IOProc de sólo entrada sobre la entrada activa
//==============================================================================
#if JUCE_MAC
namespace
{
    AudioObjectPropertyAddress makeAddress (AudioObjectPropertySelector selector,
                                            AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) noexcept
    {
        return { selector, scope, 0 };   // 0 = elemento principal (kAudioObjectPropertyElementMain)
    }

    template <typename T>
    bool readProperty (AudioObjectID object, AudioObjectPropertySelector selector,
                       AudioObjectPropertyScope scope, T& result) noexcept
    {
        const auto address = makeAddress (selector, scope);
        if (object == kAudioObjectUnknown || ! AudioObjectHasProperty (object, &address))
            return false;

        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &result) == noErr
               && size == sizeof (T);
    }

    std::vector<AudioObjectID> getAllDevices()
    {
        const auto address = makeAddress (kAudioHardwarePropertyDevices);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr
              || size < sizeof (AudioObjectID))
            return {};

        std::vector<AudioObjectID> devices (size / sizeof (AudioObjectID));
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
            return {};

        devices.resize (size / sizeof (AudioObjectID));
        return devices;
    }

    int countInputChannels (AudioObjectID device)
    {
        const auto address = makeAddress (kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput);
        UInt32 size = 0;
        if (device == kAudioObjectUnknown
              || AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr
              || size < sizeof (AudioBufferList))
            return 0;

        juce::HeapBlock<char> storage (size, true);
        auto* list = reinterpret_cast<AudioBufferList*> (storage.get());
        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, list) != noErr)
            return 0;

        int channels = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
            channels += (int) list->mBuffers[i].mNumberChannels;
        return channels;
    }

    juce::String getDeviceName (AudioObjectID device)
    {
        CFStringRef name = nullptr;
        if (readProperty (device, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, name) && name != nullptr)
        {
            auto result = juce::String::fromCFString (name);
            CFRelease (name);
            return result;
        }
        return juce::String::fromUTF8 ("Entrada de audio");
    }

    UInt32 getTransportType (AudioObjectID device)
    {
        UInt32 transport = 0;
        readProperty (device, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, transport);
        return transport;
    }

    bool isBluetooth (AudioObjectID device)
    {
        const auto t = getTransportType (device);
        return t == kAudioDeviceTransportTypeBluetooth || t == kAudioDeviceTransportTypeBluetoothLE;
    }

    bool deviceIsAlive (AudioObjectID device)
    {
        UInt32 alive = 0;
        return readProperty (device, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, alive) && alive != 0;
    }

    bool isUsableInput (AudioObjectID device)
    {
        return device != kAudioObjectUnknown && deviceIsAlive (device) && countInputChannels (device) > 0;
    }

    double getNominalSampleRate (AudioObjectID device)
    {
        Float64 rate = 0.0;
        readProperty (device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
        return (double) rate;
    }

    // El IOProc recibe el audio en el formato "virtual" de cada flujo: debe ser float de 32 bits
    bool firstInputStreamIsFloat32 (AudioObjectID device)
    {
        const auto address = makeAddress (kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr || size < sizeof (AudioStreamID))
            return false;

        std::vector<AudioStreamID> streams (size / sizeof (AudioStreamID));
        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, streams.data()) != noErr || streams.empty())
            return false;

        AudioStreamBasicDescription format {};
        if (! readProperty (streams[0], kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, format))
            return false;

        return format.mFormatID == kAudioFormatLinearPCM
               && (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0
               && format.mBitsPerChannel == 32;
    }

    // Qué flujos usa NUESTRO IOProc (propiedad del IOProc, no del dispositivo):
    // salida = ninguno (0 canales de salida), entrada = sólo el primero (canal 1).
    void setIOProcStreamUsage (AudioObjectID device, AudioDeviceIOProcID proc, bool input)
    {
        const auto address = makeAddress (kAudioDevicePropertyIOProcStreamUsage,
                                          input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput);
        UInt32 size = 0;
        if (! AudioObjectHasProperty (device, &address)
              || AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr
              || size < sizeof (AudioHardwareIOProcStreamUsage))
            return;

        juce::HeapBlock<char> storage (size, true);
        auto* usage = reinterpret_cast<AudioHardwareIOProcStreamUsage*> (storage.get());
        static_assert (sizeof (proc) == sizeof (usage->mIOProc), "AudioDeviceIOProcID ocupa lo mismo que un puntero");
        std::memcpy (&usage->mIOProc, &proc, sizeof (proc));   // el ID del IOProc identifica a NUESTRO IOProc
        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, usage) != noErr || usage->mNumberStreams == 0)
            return;

        for (UInt32 i = 0; i < usage->mNumberStreams; ++i)
            usage->mStreamIsOn[i] = (input && i == 0) ? 1u : 0u;

        AudioObjectSetPropertyData (device, &address, 0, nullptr, size, usage);   // si falla, da igual
    }

    //==========================================================================
    class CoreAudioBackend final : public SystemMicInput::Backend
    {
    public:
        ~CoreAudioBackend() override { stop(); }

        bool isSupported() override { return true; }

        // Si la app anfitriona no declara NSMicrophoneUsageDescription, macOS la
        // cerraría al usar el micrófono: en ese caso no se toca nada.
        bool hostAllowsMicrophone() override
        {
            if (auto bundle = CFBundleGetMainBundle())
                return CFBundleGetValueForInfoDictionaryKey (bundle, CFSTR ("NSMicrophoneUsageDescription")) != nullptr;
            return false;
        }

        SystemMicInput::Permission getPermission() override
        {
            // Sólo para la prueba automática en una Mac sin pantalla (CI)
            if (juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_ASSUME_MIC_PERMISSION", {}) == "1")
                return SystemMicInput::Permission::Granted;

            const int status = micpermission::getStatus();
            return status > 0  ? SystemMicInput::Permission::Granted
                 : status == 0 ? SystemMicInput::Permission::NotDetermined
                               : SystemMicInput::Permission::Denied;
        }

        void requestPermission() override   { micpermission::request(); }

        SystemMicInput::DeviceChoice chooseDevice() override
        {
            SystemMicInput::DeviceChoice choice;

            AudioObjectID device = kAudioObjectUnknown;
            readProperty (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice,
                          kAudioObjectPropertyScopeGlobal, device);

            if (! isUsableInput (device))
            {
                choice.state = MicDeviceState::NoDevice;
                return choice;
            }

            if (isBluetooth (device))
            {
                // Abrir el micrófono de unos AirPods los pasa a "manos libres" y
                // cambia su salida: se usa el micrófono de la Mac en su lugar.
                AudioObjectID builtIn = kAudioObjectUnknown;
                for (auto d : getAllDevices())
                    if (getTransportType (d) == kAudioDeviceTransportTypeBuiltIn && isUsableInput (d))
                    {
                        builtIn = d;
                        break;
                    }

                if (builtIn == kAudioObjectUnknown)
                {
                    choice.state = MicDeviceState::BluetoothOnly;
                    choice.name = getDeviceName (device);
                    return choice;
                }

                device = builtIn;
                choice.replacedBluetooth = true;
            }

            choice.state = MicDeviceState::Running;
            choice.id = (uint32_t) device;
            choice.name = getDeviceName (device);
            return choice;
        }

        juce::String start (const SystemMicInput::DeviceChoice& choice, SystemMicInput::Ring& ringToFill) override
        {
            stop();

            const auto device = (AudioObjectID) choice.id;
            if (! firstInputStreamIsFloat32 (device))
                return juce::String::fromUTF8 ("Formato de entrada no compatible");

            ring = &ringToFill;   // antes de AudioDeviceStart: el IOProc lo ve ya asignado

            // NO se cambia la frecuencia de muestreo ni el tamaño de búfer del
            // dispositivo: se lee tal como lo tiene configurado macOS / el DAW.
            AudioDeviceIOProcID proc = nullptr;
            if (AudioDeviceCreateIOProcID (device, ioProc, this, &proc) != noErr || proc == nullptr)
                return juce::String::fromUTF8 ("No se pudo abrir la entrada");

            setIOProcStreamUsage (device, proc, false);   // 0 canales de salida
            setIOProcStreamUsage (device, proc, true);    // entrada: canal 1 (primer flujo)

            if (AudioDeviceStart (device, proc) != noErr)
            {
                AudioDeviceDestroyIOProcID (device, proc);
                return juce::String::fromUTF8 ("No se pudo iniciar la entrada");
            }

            deviceId = device;
            procId = proc;
            return {};
        }

        void stop() override
        {
            if (procId != nullptr)
            {
                // Detiene SÓLO este IOProc (el dispositivo sigue para FL Studio u
                // otros programas). Desde fuera del hilo de E/S, AudioDeviceStop
                // espera a que termine la llamada en curso: después ya no hay más.
                AudioDeviceStop (deviceId, procId);
                AudioDeviceDestroyIOProcID (deviceId, procId);
                procId = nullptr;
            }

            deviceId = kAudioObjectUnknown;
        }

        bool isAlive() override             { return deviceId != kAudioObjectUnknown && deviceIsAlive (deviceId); }
        double getSampleRate() override    { return getNominalSampleRate (deviceId); }
        uint64_t getFramesReceived() const override { return framesReceived.load (std::memory_order_relaxed); }

    private:
        // Hilo de E/S de CoreAudio (tiempo real): sólo copia el canal 1 al búfer
        // circular. Sin candados, sin memoria dinámica y sin tocar la salida.
        static OSStatus ioProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                                const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* clientData)
        {
            auto& self = *static_cast<CoreAudioBackend*> (clientData);

            if (input == nullptr || input->mNumberBuffers == 0 || self.ring == nullptr)
                return noErr;

            const auto& first = input->mBuffers[0];
            if (first.mData == nullptr || first.mNumberChannels == 0)
                return noErr;

            const auto stride = (int) first.mNumberChannels;
            const auto frames = (int) (first.mDataByteSize / (sizeof (float) * (UInt32) stride));
            if (frames <= 0)
                return noErr;

            self.ring->write (static_cast<const float*> (first.mData), frames, stride);
            self.framesReceived.fetch_add ((uint64_t) frames, std::memory_order_relaxed);
            return noErr;
        }

        SystemMicInput::Ring* ring = nullptr;
        AudioObjectID deviceId = kAudioObjectUnknown;
        AudioDeviceIOProcID procId = nullptr;
        std::atomic<uint64_t> framesReceived { 0 };
    };
} // namespace

std::unique_ptr<SystemMicInput::Backend> SystemMicInput::createPlatformBackend()
{
    return std::make_unique<CoreAudioBackend>();
}

//==============================================================================
// Linux: micrófono SIMULADO para las pruebas (SmartKeys no se distribuye para
// Linux). Se activa con la variable de entorno:
//   SMARTKEYS_FAKE_MIC="sine:<Hz>:<frecuencia de muestreo>[:<amplitud>[:<periodo s>:<fracción con voz>]]"
//                      "silence:<fs>" | "none" | "bluetooth" | "fail" | "stall:<fs>"
//   SMARTKEYS_FAKE_MIC_PERMISSION = granted (por defecto) | pending | denied | nohost
// Un hilo propio entrega bloques de 512 tramas estéreo a tiempo real (como
// CoreAudio): canal 1 = la "voz"; canal 2 = un tono de 330 Hz que NO debe usarse.
//==============================================================================
#elif JUCE_LINUX
namespace
{
    class FakeMicBackend final : public SystemMicInput::Backend
    {
    public:
        ~FakeMicBackend() override { stop(); }

        bool isSupported() override              { return getSpec().isNotEmpty(); }
        bool hostAllowsMicrophone() override     { return getPermissionSpec() != "nohost"; }

        SystemMicInput::Permission getPermission() override
        {
            const auto spec = getPermissionSpec();
            if (spec == "denied")
                return SystemMicInput::Permission::Denied;

            if (spec == "pending")
            {
                const bool answered = requestedAtMs > 0.0
                                      && juce::Time::getMillisecondCounterHiRes() - requestedAtMs > 1000.0;
                return answered ? SystemMicInput::Permission::Granted : SystemMicInput::Permission::NotDetermined;
            }

            return SystemMicInput::Permission::Granted;
        }

        void requestPermission() override
        {
            requestedAtMs = juce::Time::getMillisecondCounterHiRes();
        }

        SystemMicInput::DeviceChoice chooseDevice() override
        {
            SystemMicInput::DeviceChoice choice;
            const auto spec = getSpec();

            if (spec == "none")
            {
                choice.state = MicDeviceState::NoDevice;
                return choice;
            }

            if (spec == "bluetooth")
            {
                choice.state = MicDeviceState::BluetoothOnly;
                choice.name = "AirPods (simulado)";
                return choice;
            }

            choice.state = MicDeviceState::Running;
            choice.id = (uint32_t) spec.hashCode();
            choice.name = juce::String::fromUTF8 ("Micrófono simulado (") + spec + ")";
            return choice;
        }

        juce::String start (const SystemMicInput::DeviceChoice&, SystemMicInput::Ring& ringToFill) override
        {
            stop();

            const auto config = parse (getSpec());
            if (config.failToOpen)
                return "No se pudo abrir (simulado)";

            rate.store (config.rate);
            quit.store (false);
            worker = std::thread ([this, config, &ringToFill] { run (config, ringToFill); });
            return {};
        }

        void stop() override
        {
            quit.store (true);
            if (worker.joinable())
                worker.join();
        }

        bool isAlive() override                       { return true; }
        double getSampleRate() override               { return rate.load(); }
        uint64_t getFramesReceived() const override   { return frames.load (std::memory_order_relaxed); }

    private:
        struct Config
        {
            double frequency = 220.0, rate = 48000.0, amplitude = 0.3, period = 0.0, duty = 1.0;
            bool silent = false, failToOpen = false, stall = false;
        };

        static juce::String getSpec()            { return juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_FAKE_MIC", {}); }
        static juce::String getPermissionSpec()  { return juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_FAKE_MIC_PERMISSION", {}); }

        static Config parse (const juce::String& spec)
        {
            Config c;
            const auto parts = juce::StringArray::fromTokens (spec, ":", {});
            const auto kind = parts[0];
            auto number = [&] (int index, double fallback)
            {
                return index < parts.size() && parts[index].isNotEmpty() ? parts[index].getDoubleValue() : fallback;
            };

            if (kind == "fail")
            {
                c.failToOpen = true;
            }
            else if (kind == "silence" || kind == "stall")
            {
                c.silent = true;
                c.stall = kind == "stall";
                c.rate = number (1, 48000.0);
            }
            else
            {
                c.frequency = number (1, 220.0);
                c.rate      = number (2, 48000.0);
                c.amplitude = number (3, 0.3);
                c.period    = number (4, 0.0);
                c.duty      = number (5, 1.0);
            }

            c.rate = juce::jlimit (8000.0, 192000.0, c.rate);
            return c;
        }

        void run (Config config, SystemMicInput::Ring& target)
        {
            constexpr int chunk = 512;
            std::vector<float> interleaved ((size_t) chunk * 2, 0.0f);   // hilo propio: puede asignar
            const double twoPi = juce::MathConstants<double>::twoPi;
            double voicePhase = 0.0, otherPhase = 0.0, time = 0.0;

            auto next = std::chrono::steady_clock::now();
            const auto chunkDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                                           std::chrono::duration<double> ((double) chunk / config.rate));

            while (! quit.load())
            {
                if (! config.stall)
                {
                    for (int i = 0; i < chunk; ++i)
                    {
                        const bool voiced = ! config.silent
                                            && (config.period <= 0.0 || std::fmod (time, config.period) < config.period * config.duty);
                        interleaved[(size_t) (2 * i)]     = voiced ? (float) (config.amplitude * std::sin (voicePhase)) : 0.0f;
                        interleaved[(size_t) (2 * i + 1)] = (float) (0.25 * std::sin (otherPhase));

                        voicePhase = std::fmod (voicePhase + twoPi * config.frequency / config.rate, twoPi);
                        otherPhase = std::fmod (otherPhase + twoPi * 330.0 / config.rate, twoPi);
                        time += 1.0 / config.rate;
                    }

                    target.write (interleaved.data(), chunk, 2);
                    frames.fetch_add ((uint64_t) chunk, std::memory_order_relaxed);
                }

                next += chunkDuration;
                std::this_thread::sleep_until (next);
            }
        }

        std::thread worker;
        std::atomic<bool> quit { true };
        std::atomic<double> rate { 48000.0 };
        std::atomic<uint64_t> frames { 0 };
        double requestedAtMs = 0.0;
    };
} // namespace

std::unique_ptr<SystemMicInput::Backend> SystemMicInput::createPlatformBackend()
{
    return std::make_unique<FakeMicBackend>();
}

//==============================================================================
// Windows (y cualquier otra plataforma): sin lectura directa del micrófono.
// La voz entra por la entrada del plugin (sidechain).
//==============================================================================
#else
namespace
{
    class UnsupportedBackend final : public SystemMicInput::Backend
    {
    public:
        bool isSupported() override                         { return false; }
        bool hostAllowsMicrophone() override                { return false; }
        SystemMicInput::Permission getPermission() override { return SystemMicInput::Permission::Denied; }
        void requestPermission() override                   {}
        SystemMicInput::DeviceChoice chooseDevice() override { return {}; }
        juce::String start (const SystemMicInput::DeviceChoice&, SystemMicInput::Ring&) override { return "No disponible"; }
        void stop() override                                {}
        bool isAlive() override                             { return false; }
        double getSampleRate() override                     { return 0.0; }
        uint64_t getFramesReceived() const override         { return 0; }
    };
} // namespace

std::unique_ptr<SystemMicInput::Backend> SystemMicInput::createPlatformBackend()
{
    return std::make_unique<UnsupportedBackend>();
}
#endif

} // namespace smartkeys
