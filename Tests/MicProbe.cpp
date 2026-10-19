/*
  ==============================================================================

    MicProbe.cpp
    SmartKeys — sonda del micrófono del sistema para una Mac real (o la CI).

    Usa EXACTAMENTE el código del plugin (SystemMicInput + VoicePitchTracker):
      1. Lee cómo está la entrada activa de macOS (nombre, frecuencia, búfer).
      2. La abre y la cierra varias veces seguidas (no debe pasar nada).
      3. La lee unos segundos y analiza la voz como lo hace processBlock.
      4. Comprueba que la frecuencia de muestreo y el tamaño de búfer del
         dispositivo NO cambiaron (SmartKeys nunca debe tocarlos).

    Uso:  SmartKeysMicProbe [segundos] [--expect-hz <Hz>] [--require-audio]
    Salida: 0 = bien; 2 = cambió el dispositivo; 3 = no llegó audio;
            4 = la nota detectada no es la esperada; 5 = el micrófono no se abrió.

  ==============================================================================
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include <CoreAudio/CoreAudio.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include "SystemMicInput.h"
#include "VoicePitchTracker.h"
#include "ScaleChordEngine.h"
#include "MicPermission.h"

using namespace smartkeys;

namespace
{
    template <typename T>
    bool readGlobal (AudioObjectID object, AudioObjectPropertySelector selector, T& value,
                     AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        const AudioObjectPropertyAddress address { selector, scope, 0 };
        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &value) == noErr;
    }

    struct DeviceSnapshot
    {
        AudioObjectID id = kAudioObjectUnknown;
        Float64 nominalRate = 0.0;
        UInt32 bufferFrames = 0, transport = 0;
        juce::String name;
    };

    DeviceSnapshot snapshotDefaultInput()
    {
        DeviceSnapshot s;
        readGlobal (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice, s.id);
        if (s.id == kAudioObjectUnknown)
            return s;

        readGlobal (s.id, kAudioDevicePropertyNominalSampleRate, s.nominalRate);
        readGlobal (s.id, kAudioDevicePropertyBufferFrameSize, s.bufferFrames);
        readGlobal (s.id, kAudioDevicePropertyTransportType, s.transport);

        CFStringRef name = nullptr;
        if (readGlobal (s.id, kAudioObjectPropertyName, name) && name != nullptr)
        {
            s.name = juce::String::fromCFString (name);
            CFRelease (name);
        }
        return s;
    }

    const char* stateName (MicDeviceState s)
    {
        switch (s)
        {
            case MicDeviceState::Off:               return "Off";
            case MicDeviceState::Running:           return "Running";
            case MicDeviceState::WaitingPermission: return "WaitingPermission";
            case MicDeviceState::PermissionDenied:  return "PermissionDenied";
            case MicDeviceState::HostNotAllowed:    return "HostNotAllowed";
            case MicDeviceState::NoDevice:          return "NoDevice";
            case MicDeviceState::BluetoothOnly:     return "BluetoothOnly";
            case MicDeviceState::Unsupported:       return "Unsupported";
            case MicDeviceState::Error:             return "Error";
        }
        return "?";
    }

    void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }
}

int main (int argc, char* argv[])
{
    double seconds = 4.0, expectHz = 0.0;
    bool requireAudio = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--expect-hz" && i + 1 < argc)  expectHz = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--require-audio")         requireAudio = true;
        else if (arg.getDoubleValue() > 0.0)       seconds = arg.getDoubleValue();
    }

    const auto before = snapshotDefaultInput();
    std::cout << "Entrada por defecto: " << before.name << "  id=" << before.id
              << "  " << before.nominalRate << " Hz  buffer=" << before.bufferFrames
              << "  transporte='" << juce::String::toHexString ((int) before.transport) << "'" << std::endl;
    std::cout << "Permiso de micrófono (AVFoundation): " << micpermission::getStatus()
              << "  (1 = permitido, 0 = sin preguntar, -1 = denegado)" << std::endl;

    SystemMicInput mic;

    // 1) Abrir y cerrar varias veces seguidas
    for (int i = 0; i < 6; ++i)
    {
        mic.update (true);
        std::cout << "  ciclo " << i << ": " << stateName (mic.getState()) << std::endl;
        sleepMs (i % 2 == 0 ? 30 : 120);
        mic.update (false);
    }

    // 2) Lectura continua, analizada como en processBlock
    VoicePitchTracker tracker;
    ScaleChordEngine engine;
    tracker.setGateThresholdDb (-70.0f);
    double trackerRate = 0.0;
    uint32_t trackerGeneration = 0;

    std::vector<float> scratch ((size_t) SystemMicInput::kRingCapacity);
    uint64_t samples = 0;
    double sumSquares = 0.0, freqSum = 0.0;
    int freqCount = 0, updates = 0;

    const auto start = std::chrono::steady_clock::now();
    auto nextUpdate = start;
    while (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() < seconds)
    {
        if (std::chrono::steady_clock::now() >= nextUpdate)
        {
            mic.update (true);   // como el Timer del plugin: 4 veces por segundo
            ++updates;
            nextUpdate += std::chrono::milliseconds (250);
        }

        if (mic.isRunning())
        {
            const double rate = mic.getSampleRate();
            if (std::abs (rate - trackerRate) > 0.5 || mic.getGeneration() != trackerGeneration)
            {
                tracker.prepare (rate);
                tracker.setGateThresholdDb (-70.0f);
                trackerRate = rate;
                trackerGeneration = mic.getGeneration();
                mic.discardAll();
            }

            const int n = mic.pull (scratch.data(), (int) scratch.size(), 16383);
            if (n > 0)
            {
                samples += (uint64_t) n;
                for (int i = 0; i < n; ++i)
                    sumSquares += (double) scratch[(size_t) i] * scratch[(size_t) i];

                const float* channels[1] = { scratch.data() };
                tracker.process (channels, 1, n, engine);
                const float f = tracker.detectedFrequency.load();
                if (f > 0.0f)
                {
                    freqSum += f;
                    ++freqCount;
                }
            }
        }

        sleepMs (10);   // ~ un bloque de audio de un DAW
    }

    const auto finalState = mic.getState();
    const auto deviceName = mic.getDeviceName();
    const double deviceRate = mic.getSampleRate();
    mic.shutdown();

    const auto after = snapshotDefaultInput();
    const double rms = samples > 0 ? std::sqrt (sumSquares / (double) samples) : 0.0;
    const double meanHz = freqCount > 0 ? freqSum / freqCount : 0.0;
    const double expectedSamples = seconds * (deviceRate > 0.0 ? deviceRate : 48000.0);

    std::cout << "Estado: " << stateName (finalState) << "  dispositivo: " << deviceName
              << "  " << deviceRate << " Hz" << std::endl;
    std::cout << "Muestras recibidas: " << samples << " (~" << juce::roundToInt (100.0 * (double) samples / expectedSamples)
              << " % del tiempo)  RMS " << juce::Decibels::gainToDecibels ((float) rms, -150.0f) << " dBFS" << std::endl;
    std::cout << "Frecuencia detectada: " << meanHz << " Hz en " << freqCount << " bloques; nota "
              << (meanHz > 0.0 ? juce::MidiMessage::getMidiNoteName (juce::roundToInt (69.0 + 12.0 * std::log2 (meanHz / 440.0)), true, true, 4)
                               : juce::String ("--")) << std::endl;
    std::cout << "Después: " << after.nominalRate << " Hz  buffer=" << after.bufferFrames << std::endl;

    if (before.id != kAudioObjectUnknown
          && (after.nominalRate != before.nominalRate || after.bufferFrames != before.bufferFrames))
    {
        std::cout << "ERROR: SmartKeys cambió la frecuencia o el búfer del dispositivo" << std::endl;
        return 2;
    }

    if (before.id == kAudioObjectUnknown)
    {
        std::cout << "(no hay entrada de audio: sólo se comprobó que no pasa nada)" << std::endl;
        return finalState == MicDeviceState::NoDevice ? 0 : 5;
    }

    if (finalState != MicDeviceState::Running && finalState != MicDeviceState::Off)
    {
        std::cout << "El micrófono no se abrió: " << stateName (finalState) << std::endl;
        return requireAudio ? 5 : 0;
    }

    if (requireAudio && (samples < (uint64_t) (expectedSamples * 0.5) || rms < 1.0e-5))
    {
        std::cout << "ERROR: no llegó audio del micrófono" << std::endl;
        return 3;
    }

    if (expectHz > 0.0 && std::abs (12.0 * std::log2 (juce::jmax (1.0, meanHz) / expectHz)) > 0.5)
    {
        std::cout << "ERROR: se esperaba " << expectHz << " Hz" << std::endl;
        return 4;
    }

    std::cout << "OK" << std::endl;
    return 0;
}
