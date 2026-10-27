/*
  ==============================================================================

    HostedInstrument.cpp
    SmartKeys (MIWN) — La ranura de instrumento (VST3 / Audio Unit).

  ==============================================================================
*/

#include "HostedInstrument.h"
#include <cmath>
#include <cstring>

namespace smartkeys
{

namespace
{
    constexpr size_t kHostMidiBytes = 32768;   // MIDI del bloque para el instrumento (reservado)
    constexpr int    kTimerHz = 15;

    // Un plugin que devuelve NaN o infinito estropearía la mezcla del DAW
    bool allFinite (const float* p, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            uint32_t bits;
            std::memcpy (&bits, p + i, sizeof (bits));
            if ((bits & 0x7f800000u) == 0x7f800000u)
                return false;
        }
        return true;
    }

    // Salida principal estéreo (o mono), sin entradas ni salidas auxiliares si el plugin lo permite
    void configureBuses (juce::AudioPluginInstance& p)
    {
        const auto original = p.getBusesLayout();
        if (original.outputBuses.isEmpty())
            return;

        auto attempt = [&] (bool disableInputs, const juce::AudioChannelSet& mainOut)
        {
            auto layout = original;
            if (disableInputs)
                for (auto& in : layout.inputBuses)
                    in = juce::AudioChannelSet::disabled();
            for (int i = 1; i < layout.outputBuses.size(); ++i)
                layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
            layout.outputBuses.getReference (0) = mainOut;
            return layout == original || p.setBusesLayout (layout);
        };

        if (attempt (true, juce::AudioChannelSet::stereo()) || attempt (false, juce::AudioChannelSet::stereo())
             || attempt (true, juce::AudioChannelSet::mono()) || attempt (false, juce::AudioChannelSet::mono()))
            return;

        if (p.getMainBusNumOutputChannels() <= 0)
            p.enableAllBuses();
    }

    // La descripción que se guarda (con el proyecto y para volver a crearlo) es la que se pidió:
    // con ella se pudo crear. La del plugin ya creado sólo completa lo que falte (en Linux, JUCE
    // la devuelve con el .so de dentro del paquete .vst3, que no se puede volver a abrir; en
    // Windows, con el archivo de dentro de la carpeta .vst3)
    juce::PluginDescription describeLoaded (const juce::PluginDescription& requested, const juce::AudioPluginInstance& instance)
    {
        auto d = requested;
        const auto created = instance.getPluginDescription();

        auto fill = [] (juce::String& field, const juce::String& value)
        {
            if (field.trim().isEmpty())
                field = value;
        };
        fill (d.name, created.name);
        fill (d.descriptiveName, created.descriptiveName);
        fill (d.manufacturerName, created.manufacturerName);
        fill (d.version, created.version);
        fill (d.category, created.category);
        if (d.pluginFormatName.isEmpty())
            d.pluginFormatName = created.pluginFormatName;
        if (d.fileOrIdentifier.isEmpty())
            d.fileOrIdentifier = created.fileOrIdentifier;
        if (d.uniqueId == 0 && d.deprecatedUid == 0)
        {
            d.uniqueId = created.uniqueId;
            d.deprecatedUid = created.deprecatedUid;
        }
        if (d.numInputChannels == 0 && d.numOutputChannels == 0)
        {
            d.numInputChannels = created.numInputChannels;
            d.numOutputChannels = created.numOutputChannels;
        }
        d.isInstrument = d.isInstrument || created.isInstrument;
        return d;
    }

    // El sonido guardado del instrumento (texto base64 de JUCE; también base64 estándar)
    juce::MemoryBlock decodeState (const juce::String& text)
    {
        juce::MemoryBlock data;
        if (text.isEmpty() || data.fromBase64Encoding (text))
            return data;

        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64 (decoded, text))
            return decoded.getMemoryBlock();
        return {};
    }

    const char* statusName (HostedInstrument::Status s) noexcept
    {
        switch (s)
        {
            case HostedInstrument::Status::Loading:  return "loading";
            case HostedInstrument::Status::Loaded:   return "loaded";
            case HostedInstrument::Status::Missing:  return "missing";
            case HostedInstrument::Status::Failed:   return "failed";
            case HostedInstrument::Status::Crashed:  return "crashed";
            case HostedInstrument::Status::Empty:    break;
        }
        return "empty";
    }
}

//==============================================================================
struct HostedInstrument::Slot
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;

    // Preparado en el hilo de mensajes mientras el audio no lo ve
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int numChannels = 0, outFirst = 0, outChannels = 0;
    double sampleRate = 0.0;
    int maxBlock = 0;
    bool prepared = false, preparedNonRealtime = false;

    // Sólo hilo de audio: notas encendidas en el instrumento (para soltarlas siempre)
    std::array<std::array<uint8_t, 128>, 16> notes {};
    uint16_t channelsWithNotes = 0, sustainDown = 0;
    bool silenced = false;   // en BYPASS: ya se soltó todo y no se procesa

    void resetNotes() noexcept
    {
        for (auto& ch : notes)
            ch.fill (0);
        channelsWithNotes = sustainDown = 0;
    }

    void track (const juce::uint8* d, int size) noexcept
    {
        if (size < 2)
            return;

        const int type = d[0] & 0xF0;
        const int ch = d[0] & 0x0F;
        const auto bit = (uint16_t) (1u << ch);
        const int data1 = d[1] & 0x7F;

        if (type == 0x90 && size >= 3 && (d[2] & 0x7F) > 0)
        {
            auto& c = notes[(size_t) ch][(size_t) data1];
            if (c < 255)
                ++c;
            channelsWithNotes |= bit;
        }
        else if (type == 0x80 || (type == 0x90 && size >= 3))
        {
            auto& c = notes[(size_t) ch][(size_t) data1];
            if (c > 0)
                --c;
        }
        else if (type == 0xB0 && size >= 3)
        {
            if (data1 == 64)
            {
                if ((d[2] & 0x7F) >= 64) sustainDown |= bit;
                else                     sustainDown = (uint16_t) (sustainDown & ~bit);
            }
            else if (data1 == 120 || data1 == 123)
            {
                notes[(size_t) ch].fill (0);
                channelsWithNotes = (uint16_t) (channelsWithNotes & ~bit);
            }
        }
    }

    // NoteOff de todo lo encendido, pedal arriba y All Notes Off, al principio del bloque
    void addReleases() noexcept
    {
        for (int ch = 0; ch < 16; ++ch)
        {
            const auto bit = (uint16_t) (1u << ch);
            const bool hasNotes = (channelsWithNotes & bit) != 0;
            const bool sustain  = (sustainDown & bit) != 0;
            if (! hasNotes && ! sustain && ch != 0)
                continue;   // el canal 1 (el de SmartKeys) siempre

            auto& row = notes[(size_t) ch];
            for (int n = 0; n < 128; ++n)
            {
                if (row[(size_t) n] == 0)
                    continue;
                row[(size_t) n] = 0;
                const juce::uint8 off[3] = { (juce::uint8) (0x80 | ch), (juce::uint8) n, 0 };
                midi.addEvent (off, 3, 0);
            }

            const juce::uint8 pedalUp[3]     = { (juce::uint8) (0xB0 | ch), 64, 0 };
            const juce::uint8 allNotesOff[3] = { (juce::uint8) (0xB0 | ch), 123, 0 };
            midi.addEvent (pedalUp, 3, 0);
            midi.addEvent (allNotesOff, 3, 0);
        }
        channelsWithNotes = sustainDown = 0;
    }
};

//==============================================================================
// Ventana flotante con la interfaz del plugin cargado
class HostedInstrument::Window final : public juce::DocumentWindow
{
public:
    Window (HostedInstrument& o, juce::AudioProcessorEditor* editor, const juce::String& title)
        : juce::DocumentWindow (title, juce::Colour (0xFF1B1D21),
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setAlwaysOnTop (true);   // no se esconde detrás de la ventana del DAW
    }

    ~Window() override
    {
        clearContentComponent();   // el editor del plugin se destruye antes que la ventana
    }

    void closeButtonPressed() override   { owner.windowCloseRequested(); }

private:
    HostedInstrument& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Window)
};

//==============================================================================
HostedInstrument::HostedInstrument()
{
    startTimerHz (kTimerHz);
}

HostedInstrument::~HostedInstrument()
{
    shutdown();
}

void HostedInstrument::shutdown()
{
    if (shutdownDone)
        return;

    auto teardown = [this]
    {
        shutdownDone = true;
        shuttingDown = true;
        stopTimer();
        ++loadGeneration;               // una carga en curso ya no se instala
        masterReference.clear();        // ni llega ninguna respuesta pendiente
        onLatencyChanged = nullptr;

        closeWindow();                  // 1) el editor del plugin
        detach();                       // 2) el audio lo suelta
        destroySlot (std::move (current));   // 3) el plugin

        for (const auto& d : loadMarks)
            catalog->markLoadFinished (d);
        loadMarks.clear();
    };

    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr || juce::MessageManager::existsAndIsCurrentThread())
    {
        teardown();
    }
    else
    {
        // El DAW destruye SmartKeys desde otro hilo: el hilo de mensajes queda quieto mientras tanto
        const juce::MessageManagerLock lock;
        teardown();
    }
}

void HostedInstrument::setStatus (Status s) noexcept
{
    status.store ((int) s, std::memory_order_release);
    changed();
}

juce::PluginDescription HostedInstrument::getDescription() const
{
    if (current != nullptr && getStatus() == Status::Loaded)
        return current->description;

    const juce::ScopedLock sl (cacheLock);
    return cacheDescription;
}

juce::String HostedInstrument::getRecentFailure() const
{
    return juce::Time::getMillisecondCounterHiRes() - recentFailureMs < 8000.0 ? recentFailure : juce::String();
}

//==============================================================================
// Cargar (siempre en el hilo de mensajes; la creación es asíncrona)
void HostedInstrument::load (const juce::PluginDescription& d)
{
    if (shuttingDown)
        return;

    // Ya está (o se está cargando): nada que hacer
    if (current != nullptr && getStatus() == Status::Loaded && current->description.isDuplicateOf (d))
        return;
    if (getStatus() == Status::Loading && loadingDescription.isDuplicateOf (d))
        return;

    catalog->forgetCrash (d);
    bypassed.store (false);
    startLoad (d, {}, true);
}

void HostedInstrument::retry()
{
    juce::PluginDescription d;
    juce::MemoryBlock data;
    {
        const juce::ScopedLock sl (cacheLock);
        if (! cacheHasPlugin)
            return;
        d = cacheDescription;
        data = cacheState;
    }

    catalog->forgetCrash (d);
    startLoad (d, data, true);
}

void HostedInstrument::startLoad (const juce::PluginDescription& d, const juce::MemoryBlock& stateToApply, bool userAction)
{
    if (shuttingDown)
        return;

    const auto generation = ++loadGeneration;
    loadingDescription = d;
    loadingState = stateToApply;
    loadingByUser = userAction;
    errorText = {};

    // Lo que se guarda ahora con el proyecto: el instrumento pedido y su sonido
    {
        const juce::ScopedLock sl (cacheLock);
        cacheHasPlugin = true;
        cacheDescription = d;
        cacheState = stateToApply;
        cacheError = {};
    }

    // Carga protegida: si el programa se cerró la última vez creando este plugin, al
    // abrir el proyecto no se vuelve a crear solo (se puede reintentar a mano)
    if (! userAction && catalog->crashedDuringLastLoad (d))
    {
        if (current != nullptr)
            unloadInternal();
        errorText = juce::String::fromUTF8 ("Se cerró el programa al cargarlo la última vez");
        {
            const juce::ScopedLock sl (cacheLock);
            cacheError = errorText;
        }
        setStatus (Status::Crashed);
        return;
    }

    setStatus (Status::Loading);
    catalog->markLoadStarted (d);
    loadMarks.push_back (d);

    juce::WeakReference<HostedInstrument> weak (this);
    catalog->getFormatManager().createPluginInstanceAsync (d, targetRate.load(), targetBlock.load(),
        [weak, generation, d] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            // Un Audio Unit v3 puede contestar desde otro hilo: el resultado va al hilo de mensajes
            auto result = std::make_shared<std::unique_ptr<juce::AudioPluginInstance>> (std::move (instance));
            auto finish = [weak, generation, d, result, error]
            {
                if (auto* self = weak.get())
                    self->finishLoad (generation, d, std::move (*result), error);
                else
                    result->reset();
            };

            if (juce::MessageManager::existsAndIsCurrentThread())
                finish();
            else
                juce::MessageManager::callAsync (std::move (finish));
        });
}

void HostedInstrument::finishLoad (uint32_t generation, const juce::PluginDescription& requested,
                                   std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
{
    forgetLoadMark (requested);

    if (shuttingDown || generation != loadGeneration)
    {
        instance.reset();   // después se pidió otro (o quitarlo): éste ya no hace falta
        return;
    }

    juce::String problem;
    if (instance == nullptr)
    {
        problem = catalog->getFormatManager().doesPluginStillExist (requested)
                    ? (error.isNotEmpty() ? error : juce::String::fromUTF8 ("no se pudo crear"))
                    : juce::String::fromUTF8 ("no está instalado en esta computadora");
    }
    else
    {
        configureBuses (*instance);
        if (instance->getMainBusNumOutputChannels() <= 0)
        {
            problem = juce::String::fromUTF8 ("no tiene salida de audio");
            instance.reset();
        }
    }

    if (instance == nullptr)
    {
        const bool missing = ! catalog->getFormatManager().doesPluginStillExist (requested);
        recentFailure = requested.name + ": " + problem;
        recentFailureMs = juce::Time::getMillisecondCounterHiRes();

        if (loadingByUser && current != nullptr)
        {
            // Lo eligió el usuario y no se pudo: sigue sonando el que había
            errorText = {};
            setStatus (Status::Loaded);
            refreshStateCache();
            return;
        }

        // El proyecto (o un preset) pide este instrumento: se queda apuntado para no perderlo
        unloadInternal();
        errorText = problem;
        {
            const juce::ScopedLock sl (cacheLock);
            cacheError = problem;
        }
        setStatus (missing ? Status::Missing : Status::Failed);
        return;
    }

    instance->setNonRealtime (nonRealtime.load());
    if (loadingState.getSize() > 0)
        instance->setStateInformation (loadingState.getData(), (int) loadingState.getSize());

    auto slot = std::make_unique<Slot>();
    slot->description = describeLoaded (requested, *instance);
    slot->instance = std::move (instance);
    slot->midi.ensureSize (kHostMidiBytes);
    if (active.load())
        prepareSlot (*slot, targetRate.load(), targetBlock.load());
    slot->instance->addListener (this);

    // Cambio: la ventana del anterior se cierra antes de destruirlo; si estaba abierta,
    // se abre la del nuevo
    const bool hadWindow = window != nullptr;
    closeWindow();

    auto old = std::move (current);
    current = std::move (slot);
    attach();
    destroySlot (std::move (old));

    errorText = {};
    loadingState.reset();
    invalidAudio.store (false);
    setStatus (Status::Loaded);
    refreshStateCache();
    updateLatency();

    if (hadWindow)
        reopenWindow = true;
}

void HostedInstrument::forgetLoadMark (const juce::PluginDescription& d)
{
    for (auto it = loadMarks.begin(); it != loadMarks.end(); ++it)
    {
        if (it->isDuplicateOf (d) && it->pluginFormatName == d.pluginFormatName)
        {
            loadMarks.erase (it);
            catalog->markLoadFinished (d);
            return;
        }
    }
}

//==============================================================================
void HostedInstrument::unload()
{
    ++loadGeneration;   // una carga en curso ya no se instala
    {
        const juce::ScopedLock sl (cacheLock);
        cacheHasPlugin = false;
        cacheDescription = {};
        cacheState.reset();
        cacheError = {};
    }
    unloadInternal();
    errorText = {};
    setStatus (Status::Empty);
}

void HostedInstrument::unloadInternal()
{
    closeWindow();
    reopenWindow = false;
    detach();
    destroySlot (std::move (current));
    updateLatency();
    changed();
}

void HostedInstrument::destroySlot (std::unique_ptr<Slot> s)
{
    // Precondición: su ventana está cerrada y el audio ya no lo ve (detach / attach)
    if (s == nullptr || s->instance == nullptr)
        return;

    s->instance->removeListener (this);
    if (s->prepared)
        s->instance->releaseResources();
    s->instance.reset();
}

void HostedInstrument::detach()
{
    const juce::SpinLock::ScopedLockType lock (processLock);
    live = nullptr;
}

void HostedInstrument::attach()
{
    const juce::SpinLock::ScopedLockType lock (processLock);
    live = current.get();
}

void HostedInstrument::setBypassed (bool shouldBypass)
{
    if (bypassed.exchange (shouldBypass) != shouldBypass)
        changed();
}

//==============================================================================
// Preparar / liberar (lo pide el DAW a SmartKeys, desde cualquier hilo)
void HostedInstrument::prepare (double sampleRate, int maxBlockSize)
{
    targetRate.store (sampleRate > 0.0 ? sampleRate : 44100.0);
    targetBlock.store (juce::jmax (16, maxBlockSize));
    active.store (true);
    releaseAllNotes();   // SmartKeys empieza de cero: el instrumento también

    if (juce::MessageManager::existsAndIsCurrentThread())
        reconcile();
    else
        reconcilePending.store (true);
}

void HostedInstrument::release()
{
    active.store (false);

    if (juce::MessageManager::existsAndIsCurrentThread())
        reconcile();
    else
        reconcilePending.store (true);
}

void HostedInstrument::setNonRealtime (bool isNonRealtime) noexcept
{
    nonRealtime.store (isNonRealtime);
}

void HostedInstrument::reconcile()
{
    reconcilePending.store (false);
    if (current == nullptr || shuttingDown)
        return;

    auto& s = *current;
    const double rate = targetRate.load();
    const int block = targetBlock.load();

    if (active.load())
    {
        if (s.prepared && std::abs (s.sampleRate - rate) < 0.5 && s.maxBlock >= block
              && s.preparedNonRealtime == nonRealtime.load())
            return;

        detach();   // mientras se prepara, el audio sale en silencio
        prepareSlot (s, rate, block);
        attach();
    }
    else if (s.prepared)
    {
        detach();
        s.instance->releaseResources();
        s.prepared = false;
        attach();
    }

    latencyDirty.store (true);   // el temporizador avisa al DAW (no dentro de prepareToPlay)
}

void HostedInstrument::prepareSlot (Slot& s, double rate, int block)
{
    auto& p = *s.instance;
    if (s.prepared)
    {
        p.releaseResources();
        s.prepared = false;
    }

    block = juce::jmax (16, block);
    p.setNonRealtime (nonRealtime.load());
    p.setRateAndBufferSizeDetails (rate, block);
    p.prepareToPlay (rate, block);

    s.sampleRate = rate;
    s.maxBlock = block;
    s.preparedNonRealtime = nonRealtime.load();
    s.numChannels = juce::jmax (1, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    s.outFirst = juce::jmax (0, p.getChannelIndexInProcessBlockBuffer (false, 0, 0));
    s.outChannels = juce::jmin (p.getMainBusNumOutputChannels(), s.numChannels - s.outFirst);
    s.scratch.setSize (s.numChannels, block, false, true, false);
    s.midi.ensureSize (kHostMidiBytes);
    s.resetNotes();
    s.silenced = false;
    s.prepared = s.outChannels > 0;

    // VST3: la lista de eventos que JUCE le pasa al plugin crece (pide memoria) la primera
    // vez que un bloque trae más eventos que nunca. Un bloque de preparación con el máximo
    // que admite VST3 (2048) de eventos inocuos (presión polifónica 0 en el canal 16) la deja
    // ya con su tamaño final: después, el hilo de audio nunca le hace pedir memoria.
    if (s.prepared && s.description.pluginFormatName == "VST3")
    {
        s.midi.clear();
        for (int i = 0; i < 2048; ++i)
        {
            const juce::uint8 pressure[3] = { 0xAF, (juce::uint8) (i & 0x7F), 0 };
            s.midi.addEvent (pressure, 3, (int) ((juce::int64) i * (block - 1) / 2047));
        }
        s.scratch.clear();
        p.processBlock (s.scratch, s.midi);
        s.midi.clear();
        s.scratch.clear();
    }
}

void HostedInstrument::updateLatency()
{
    latencyDirty.store (false);

    int newLatency = 0;
    double tail = 0.3;
    if (current != nullptr && current->prepared && getStatus() == Status::Loaded)
    {
        newLatency = juce::jmax (0, current->instance->getLatencySamples());
        tail = current->instance->getTailLengthSeconds();
        if (std::isnan (tail) || tail < 0.0)
            tail = 0.3;
    }
    tailSeconds.store (tail);

    if (latency.exchange (newLatency) != newLatency && onLatencyChanged != nullptr)
        onLatencyChanged (newLatency);
}

//==============================================================================
// Estado (lo guarda el DAW con el proyecto)
std::unique_ptr<juce::XmlElement> HostedInstrument::createStateXml()
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        refreshStateCache();                             // lo más reciente del plugin
    else
        stateReadOffMessageThread.store (true);          // la copia (se renueva al dejar de tocarlo)

    const juce::ScopedLock sl (cacheLock);
    if (! cacheHasPlugin)
        return nullptr;

    auto xml = std::make_unique<juce::XmlElement> (kStateTag);
    xml->setAttribute ("bypass", bypassed.load() ? 1 : 0);
    xml->setAttribute ("status", statusName (getStatus()));
    if (cacheError.isNotEmpty())
        xml->setAttribute ("error", cacheError);
    xml->addChildElement (cacheDescription.createXml().release());
    if (cacheState.getSize() > 0)
        xml->createNewChildElement ("STATE")->addTextElement (cacheState.toBase64Encoding());
    return xml;
}

void HostedInstrument::refreshStateCache()
{
    stateRefreshPending = false;
    if (current == nullptr || getStatus() != Status::Loaded)
        return;

    juce::MemoryBlock data;
    current->instance->getStateInformation (data);

    const juce::ScopedLock sl (cacheLock);
    cacheHasPlugin = true;
    cacheDescription = current->description;
    cacheState = std::move (data);
}

void HostedInstrument::restoreState (const juce::XmlElement* xml)
{
    bool hasPlugin = false, bypass = false;
    juce::PluginDescription d;
    juce::MemoryBlock data;

    if (xml != nullptr)
    {
        if (auto* p = xml->getChildByName ("PLUGIN"))
            hasPlugin = d.loadFromXml (*p) && d.fileOrIdentifier.isNotEmpty();
        if (hasPlugin)
            data = decodeState (xml->getChildElementAllSubText ("STATE", {}).trim());
        bypass = xml->getBoolAttribute ("bypass", false);
    }

    bypassed.store (hasPlugin && bypass);
    {
        const juce::ScopedLock sl (cacheLock);
        cacheHasPlugin = hasPlugin;
        cacheDescription = d;
        cacheState = data;
        cacheError = {};
        restorePending = true;
        restoreHasPlugin = hasPlugin;
        restoreDescription = d;
        restoreStateData = data;
    }
    changed();

    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPendingRestore();
}

void HostedInstrument::applyPendingRestore()
{
    bool hasPlugin = false;
    juce::PluginDescription d;
    juce::MemoryBlock data;
    {
        const juce::ScopedLock sl (cacheLock);
        if (! restorePending)
            return;
        restorePending = false;
        hasPlugin = restoreHasPlugin;
        d = restoreDescription;
        data = restoreStateData;
        restoreStateData.reset();
    }

    if (shuttingDown)
        return;

    if (! hasPlugin)
    {
        ++loadGeneration;
        unloadInternal();
        errorText = {};
        setStatus (Status::Empty);
        return;
    }

    // El mismo instrumento ya cargado: sólo su sonido (más rápido; su ventana sigue abierta)
    if (current != nullptr && getStatus() == Status::Loaded && current->description.isDuplicateOf (d))
    {
        if (data.getSize() > 0)
            current->instance->setStateInformation (data.getData(), (int) data.getSize());
        refreshStateCache();
        changed();
        return;
    }

    startLoad (d, data, false);
}

//==============================================================================
// Ventana del plugin
void HostedInstrument::showWindow (juce::Component* nextTo)
{
    if (window != nullptr)
    {
        window->setVisible (true);
        window->toFront (true);
        return;
    }

    if (current == nullptr || getStatus() != Status::Loaded || shuttingDown)
        return;

    // createEditorAndMakeActive() es el nombre en JUCE 8 de createEditorIfNeeded(): devuelve el
    // editor activo del plugin o lo crea (la ventana es su dueña y lo destruye al cerrarse)
    auto& p = *current->instance;
    juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (p);   // un plugin sin interfaz: sus parámetros

    window = std::make_unique<Window> (*this, editor, juce::String::fromUTF8 ("SmartKeys  ·  ") + current->description.name);
    placeWindow (*window, nextTo);
    window->setVisible (true);
   #if JUCE_MAC
    if (auto* peer = window->getPeer())
        configureHostedPluginWindow (peer->getNativeHandle());
   #endif
    window->toFront (true);

    if (nextTo != nullptr)
        reopenNextTo = nextTo;
    changed();
}

void HostedInstrument::closeWindow()
{
    if (window == nullptr)
        return;

    window = nullptr;   // ~Window destruye primero el editor del plugin
    if (! shuttingDown)
        refreshStateCache();   // lo que se tocó en la ventana queda guardado
    changed();
}

void HostedInstrument::windowCloseRequested()
{
    reopenWindow = false;
    juce::WeakReference<HostedInstrument> weak (this);
    juce::MessageManager::callAsync ([weak]
    {
        if (auto* self = weak.get())
            self->closeWindow();
    });
}

void HostedInstrument::smartKeysEditorOpened (juce::Component& editor)
{
    reopenNextTo = &editor;
}

void HostedInstrument::smartKeysEditorClosing()
{
    const bool wasOpen = window != nullptr;
    closeWindow();
    reopenWindow = wasOpen;   // al abrir otra vez SmartKeys, vuelve a abrirse
    reopenNextTo = nullptr;
}

void HostedInstrument::placeWindow (Window& w, juce::Component* nextTo)
{
    const int width = w.getWidth(), height = w.getHeight();

    if (nextTo == nullptr || ! nextTo->isShowing())
    {
        w.centreWithSize (width, height);
        return;
    }

    // Al lado de la ventana de SmartKeys (a la derecha si cabe; si no, a la izquierda)
    const auto anchor = nextTo->getScreenBounds();
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (anchor);
    const auto area = display != nullptr ? display->userBounds.toNearestInt() : anchor;

    int x = anchor.getRight() + 12;
    if (x + width > area.getRight())
        x = anchor.getX() - width - 12;
    if (x < area.getX())
        x = anchor.getCentreX() - width / 2;

    x = juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - width), x);
    const int y = juce::jlimit (area.getY(), juce::jmax (area.getY(), area.getBottom() - height), anchor.getY());
    w.setTopLeftPosition (x, y);
}

//==============================================================================
void HostedInstrument::timerCallback()
{
    if (shuttingDown)
        return;

    if (reconcilePending.load())
        reconcile();

    applyPendingRestore();

    if (latencyDirty.load())
        updateLatency();

    // Copia del estado para un DAW que lo pide desde otro hilo: se renueva cuando
    // se deja de tocar el plugin (no en cada movimiento de una perilla)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (stateDirty.exchange (false))
    {
        stateRefreshPending = true;
        lastStateChangeMs = now;
    }
    if (stateRefreshPending && stateReadOffMessageThread.load() && now - lastStateChangeMs > 1500.0)
        refreshStateCache();

    // La ventana del plugin vuelve a abrirse con la de SmartKeys (o tras cambiar de instrumento)
    if (reopenWindow && window == nullptr && getStatus() == Status::Loaded)
    {
        if (auto* next = reopenNextTo.getComponent(); next != nullptr && next->isShowing())
        {
            reopenWindow = false;
            showWindow (next);
        }
    }
}

void HostedInstrument::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{
    stateDirty.store (true, std::memory_order_relaxed);   // puede llegar desde el hilo de audio
}

void HostedInstrument::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.latencyChanged)
        latencyDirty.store (true);
    if (details.nonParameterStateChanged || details.programChanged || details.parameterInfoChanged)
        stateDirty.store (true, std::memory_order_relaxed);
}

//==============================================================================
// HILO DE AUDIO: sin memoria dinámica, sin esperar a nadie
bool HostedInstrument::process (juce::AudioBuffer<float>& buffer, int firstChannel, int numChannels,
                                const juce::MidiBuffer& midi, int numSamples, juce::AudioPlayHead* playHead) noexcept
{
    if (numSamples <= 0 || numChannels <= 0 || firstChannel < 0 || firstChannel + numChannels > buffer.getNumChannels())
        return false;

    const juce::SpinLock::ScopedTryLockType lock (processLock);
    if (! lock.isLocked())
        return false;   // el hilo de mensajes está cambiando el instrumento: este bloque, silencio

    auto* s = live;
    if (s == nullptr || ! s->prepared || ! active.load (std::memory_order_relaxed)
          || std::abs (s->sampleRate - targetRate.load (std::memory_order_relaxed)) > 0.5)
        return false;

    s->instance->setPlayHead (playHead);

    if (bypassed.load (std::memory_order_relaxed))
    {
        if (s->silenced)
            return false;

        // BYPASS: un último bloque que suelta todas sus notas y se apaga con un fundido
        releaseRequested.store (false, std::memory_order_relaxed);
        render (*s, buffer, firstChannel, numChannels, nullptr, numSamples, true);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGainRamp (firstChannel + ch, 0, numSamples, 1.0f, 0.0f);
        s->silenced = true;
        return true;
    }

    const bool fadeIn = s->silenced;
    s->silenced = false;
    const bool releaseFirst = releaseRequested.exchange (false, std::memory_order_acq_rel);
    render (*s, buffer, firstChannel, numChannels, &midi, numSamples, releaseFirst);

    if (fadeIn)
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGainRamp (firstChannel + ch, 0, numSamples, 0.0f, 1.0f);
    return true;
}

void HostedInstrument::render (Slot& s, juce::AudioBuffer<float>& out, int firstChannel, int numChannels,
                               const juce::MidiBuffer* midi, int numSamples, bool releaseFirst) noexcept
{
    // Bloques más largos que el que se preparó: en trozos (con su MIDI en su sitio)
    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, s.maxBlock);

        s.midi.clear();
        if (releaseFirst && done == 0)
            s.addReleases();

        if (midi != nullptr)
        {
            for (auto it = midi->findNextSamplePosition (done); it != midi->cend(); ++it)
            {
                const auto meta = *it;
                if (meta.samplePosition >= done + n)
                    break;
                if (meta.data == nullptr || meta.numBytes <= 0)
                    continue;
                s.midi.addEvent (meta.data, meta.numBytes, juce::jmax (0, meta.samplePosition - done));
                s.track (meta.data, meta.numBytes);
            }
        }

        s.scratch.setSize (s.numChannels, n, false, false, true);   // sin reservar memoria
        s.scratch.clear();
        s.instance->processBlock (s.scratch, s.midi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const int source = s.outFirst + juce::jmin (ch, s.outChannels - 1);   // mono -> L y R
            juce::FloatVectorOperations::copy (out.getWritePointer (firstChannel + ch, done),
                                               s.scratch.getReadPointer (source), n);
        }

        done += n;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (! allFinite (out.getReadPointer (firstChannel + ch), numSamples))
        {
            for (int c = 0; c < numChannels; ++c)
                out.clear (firstChannel + c, 0, numSamples);
            invalidAudio.store (true, std::memory_order_relaxed);
            break;
        }
    }
}

} // namespace smartkeys
