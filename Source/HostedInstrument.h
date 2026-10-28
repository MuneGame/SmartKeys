/*
  ==============================================================================

    HostedInstrument.h
    SmartKeys (MIWN) — La ranura de instrumento (2.0, estilo Kontakt / PatchWork).

    SmartKeys ya no suena por sí mismo: los acordes completos que genera (con
    su STRUM, de las teclas, el Piano Roll, el teclado de la PC o la voz) van
    al instrumento VST3 / Audio Unit cargado aquí, y el audio de ese
    instrumento sale por la salida de SmartKeys.

    La interfaz del instrumento se muestra DENTRO de la ventana de SmartKeys
    (vista SYNTH), nunca en una ventana flotante aparte: HostedInstrument es
    dueño de ese editor y lo destruye antes que el instrumento.

    Hilos:
      - Crear, cambiar y quitar el instrumento, su interfaz y su estado: SÓLO en
        el hilo de mensajes. Lo que el DAW pide desde otro hilo (prepareToPlay,
        get/setStateInformation) se apunta en atómicos y se aplica después en
        el hilo de mensajes (temporizador).
      - Hilo de audio: process() toma el instrumento con un try-lock
        (juce::SpinLock). Si en ese instante el hilo de mensajes lo está
        cambiando, ese bloque sale en silencio: el audio nunca espera y nunca
        toca un instrumento a medio crear o ya destruido. El hilo de mensajes
        sólo sujeta el candado para cambiar un puntero.
      - El instrumento viejo se destruye en el hilo de mensajes, siempre
        después de destruir su interfaz y de que el audio lo haya soltado.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "PluginCatalog.h"

namespace smartkeys
{

class HostedInstrument : private juce::AudioProcessorListener,
                         private juce::Timer
{
public:
    enum class Status { Empty = 0, Loading, Loaded, Missing, Failed, Crashed };

    HostedInstrument();
    ~HostedInstrument() override;

    // SmartKeys informa al DAW de la latencia del instrumento (hilo de mensajes)
    std::function<void (int latencySamples)> onLatencyChanged;

    //==========================================================================
    // Hilo de mensajes (la interfaz)
    void load (const juce::PluginDescription&);
    void retry();                                   // vuelve a cargar el que falló, con su sonido guardado
    void unload();
    void setBypassed (bool shouldBypass);
    bool isBypassed() const noexcept                { return bypassed.load (std::memory_order_relaxed); }

    // La interfaz del instrumento cargado, para mostrarla DENTRO de la ventana de SmartKeys
    // (vista SYNTH). HostedInstrument es su dueño: quien la muestra sólo la añade como hija.
    // nullptr si no hay un instrumento cargado. Un plugin sin interfaz: sus parámetros.
    // Al cambiar o quitar el instrumento (y al cerrar SmartKeys) se destruye antes que él;
    // su componente desaparece solo de donde estaba. El MIDI y el audio nunca dependen de ella.
    juce::AudioProcessorEditor* getEditor (bool createIfNeeded);
    void releaseEditor();                           // la destruye (guarda antes el sonido del plugin)

    Status getStatus() const noexcept               { return (Status) status.load (std::memory_order_acquire); }
    juce::PluginDescription getDescription() const;
    juce::String getErrorText() const               { return errorText; }
    juce::String getRecentFailure() const;          // "Serum: no se pudo crear" (unos segundos)
    bool producedInvalidAudio() const noexcept      { return invalidAudio.load (std::memory_order_relaxed); }
    uint32_t getChangeCounter() const noexcept      { return changeCounter.load (std::memory_order_acquire); }
    PluginCatalog& getCatalog() noexcept            { return *catalog; }

    // Antes de destruir SmartKeys (destruye su interfaz y el instrumento)
    void shutdown();

    //==========================================================================
    // Cualquier hilo (llamadas del DAW a SmartKeys)
    void prepare (double sampleRate, int maxBlockSize);
    void release();
    void setNonRealtime (bool isNonRealtime) noexcept;
    std::unique_ptr<juce::XmlElement> createStateXml();
    void restoreState (const juce::XmlElement* stateOrNull);   // nullptr: sin instrumento
    int getLatencySamples() const noexcept          { return latency.load (std::memory_order_relaxed); }
    double getTailLengthSeconds() const noexcept    { return tailSeconds.load (std::memory_order_relaxed); }

    static constexpr const char* kStateTag = "HostedPlugin";

    //==========================================================================
    // Hilo de audio. Renderiza el instrumento con 'midi' en los canales
    // [firstChannel, firstChannel + numChannels) de 'buffer'. Devuelve false si
    // no sonó nada (sin instrumento, en bypass, cargándose o cambiándose):
    // esos canales no se tocan (SmartKeys ya los dejó en silencio).
    bool process (juce::AudioBuffer<float>& buffer, int firstChannel, int numChannels,
                  const juce::MidiBuffer& midi, int numSamples, juce::AudioPlayHead* playHead) noexcept;

    // El siguiente process() suelta antes todas las notas que el instrumento
    // tenga encendidas (y el pedal): SmartKeys olvidó las suyas (prepareToPlay, bypass)
    void releaseAllNotes() noexcept                 { releaseRequested.store (true, std::memory_order_release); }

private:
    struct Slot;

    void startLoad (const juce::PluginDescription&, const juce::MemoryBlock& stateToApply, bool userAction);
    void finishLoad (uint32_t generation, const juce::PluginDescription& requested,
                     std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error);
    void setStatus (Status) noexcept;
    void changed() noexcept                         { changeCounter.fetch_add (1, std::memory_order_acq_rel); }

    void reconcile();                               // prepara / libera según lo que pidió el DAW
    void prepareSlot (Slot&, double rate, int block);
    void detach();
    void attach();
    void destroySlot (std::unique_ptr<Slot>);
    void unloadInternal();
    void applyPendingRestore();
    void refreshStateCache();
    void updateLatency();
    void forgetLoadMark (const juce::PluginDescription&);

    void render (Slot&, juce::AudioBuffer<float>&, int firstChannel, int numChannels,
                 const juce::MidiBuffer* midi, int numSamples, bool releaseFirst) noexcept;

    // Un bloque que el instrumento no pudo procesar (se estaba cambiando o preparando): si traía
    // un NoteOff, el siguiente bloque empieza soltando todo (nunca queda una nota colgada)
    void noteMissedBlock (const juce::MidiBuffer& midi) noexcept;

    void timerCallback() override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;

    //==========================================================================
    juce::SharedResourcePointer<PluginCatalog> catalog;

    // Lo que procesa el hilo de audio (protegido por processLock)
    juce::SpinLock processLock;
    Slot* live = nullptr;

    // Hilo de mensajes
    std::unique_ptr<Slot> current;
    std::unique_ptr<juce::AudioProcessorEditor> editor;   // su interfaz (dentro de la ventana de SmartKeys)
    uint32_t loadGeneration = 0;
    juce::PluginDescription loadingDescription;
    juce::MemoryBlock loadingState;
    std::vector<juce::PluginDescription> loadMarks;
    bool loadingByUser = false;
    juce::String errorText, recentFailure;
    double recentFailureMs = -1.0e9;
    bool shuttingDown = false, shutdownDone = false;
    bool stateRefreshPending = false;
    double lastStateChangeMs = 0.0;

    // Compartido entre hilos
    std::atomic<int>    status { (int) Status::Empty };
    std::atomic<bool>   bypassed { false }, releaseRequested { false }, invalidAudio { false };
    std::atomic<bool>   active { false }, reconcilePending { false }, nonRealtime { false };
    std::atomic<double> targetRate { 44100.0 };
    std::atomic<int>    targetBlock { 512 };
    std::atomic<int>    latency { 0 };
    std::atomic<bool>   latencyDirty { false }, stateDirty { false }, stateReadOffMessageThread { false };
    std::atomic<double> tailSeconds { 0.3 };
    std::atomic<uint32_t> changeCounter { 0 };

    // Copia del estado para el DAW si lo pide desde otro hilo (y del instrumento que
    // falta, para que guardar el proyecto no lo pierda)
    juce::CriticalSection cacheLock;
    bool cacheHasPlugin = false;
    juce::PluginDescription cacheDescription;
    juce::MemoryBlock cacheState;
    juce::String cacheError;   // por qué no se cargó (se guarda para diagnóstico; no se lee al abrir)
    bool restorePending = false, restoreHasPlugin = false;
    juce::PluginDescription restoreDescription;
    juce::MemoryBlock restoreStateData;

    JUCE_DECLARE_WEAK_REFERENCEABLE (HostedInstrument)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedInstrument)
};

} // namespace smartkeys
