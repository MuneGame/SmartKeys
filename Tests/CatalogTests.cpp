/*
  ==============================================================================

    CatalogTests.cpp
    SmartKeys — prueba del catálogo de instrumentos de la ranura (2.0) y de los
    presets propios:

      0. Qué se puede cargar en la ranura: sólo instrumentos (isInstrument), ni
         SmartKeys ni anfitriones de plugins / patchers / navegadores de samples,
         ni utilidades de ruteo que se declaran instrumento (AGridder, Element),
         ni nombres o categorías de efecto, analizador, ruteo, MIDI, utilidad o
         control, ni sin salida de audio; Kontakt, Analog Lab, Omnisphere… sí.
      1. Audio Unit (macOS): la lista de instrumentos del sistema sale sin abrir
         ningún plugin (DLSMusicDevice de Apple siempre está; el instrumento de
         prueba si se instaló; el efecto de prueba nunca) y SmartKeys no se
         ofrece a sí mismo.
      2. VST3: la búsqueda (VST3_PATH) encuentra el instrumento de prueba, se
         salta SmartKeys.vst3, deja fuera el efecto de prueba (y lo aparta),
         se guarda en disco y la próxima vez ya se sabe.
      3. Un plugin que cerró el programa al examinarlo no se vuelve a examinar.
      4. Carga protegida: una carga que no terminó (el programa se cerró) se
         recuerda al abrir otra vez.
      5. Presets: los de fábrica, guardar, listar, cargar y borrar los del usuario.
      6. La ranura por dentro (HostedInstrument, sin DAW): un bloque más grande
         que el preparado llega al instrumento en trozos con cada nota en su
         muestra; y un hilo de audio procesa sin parar mientras el hilo de
         mensajes carga, quita, pone en BYPASS y vuelve a preparar el instrumento.
      7. Sólo instrumentos: "Buscar nuevos" no vuelve a abrir un efecto apartado
         (aunque cambie), "Volver a buscar todos" sí; y una lista guardada por
         una versión anterior con efectos o anfitriones se limpia al abrir.
      8. Un VST3 que se declara instrumento pero se llama "AGridder Inst" o
         "Element" no se ofrece (se aparta); uno que al cargarlo no tiene salida
         de audio sale de la lista y se recuerda.

    Uso: SmartKeysCatalogTests <carpeta con SmartKeysTestSynth.vst3 y SmartKeysTestFx.vst3 [y SmartKeys.vst3]>
         (SMARTKEYS_EXPECT_TEST_AU=1: el AU de prueba y el AU de efecto de prueba deben estar registrados)

  ==============================================================================
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include "PluginCatalog.h"
#include "Presets.h"
#include "HostedInstrument.h"
#include <atomic>
#include <random>
#include <thread>

using namespace juce;
using smartkeys::PluginCatalog;

namespace
{
int numChecks = 0, numFailures = 0;

void check (bool ok, const char* what)
{
    ++numChecks;
    if (! ok) ++numFailures;
    std::cout << (ok ? "  ok     " : "  FALLO  ") << what << std::endl;
}

bool contains (const Array<PluginDescription>& list, const String& name, const String& format)
{
    for (const auto& d : list)
        if (d.name == name && d.pluginFormatName == format)
            return true;
    return false;
}

bool listedByName (const Array<PluginDescription>& list, const String& name)
{
    for (const auto& d : list)
        if (d.name == name)
            return true;
    return false;
}

void scanAndWait (PluginCatalog& catalog, bool everything)
{
    catalog.startVst3Scan (everything);
    const auto end = Time::getMillisecondCounterHiRes() + 60000.0;
    while (catalog.isScanning() && Time::getMillisecondCounterHiRes() < end)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
}

// La lista VST3 que SmartKeys guarda en disco (la misma que lee al abrir)
File savedListFile()
{
    return PluginCatalog::getUserDataFolder().getChildFile ("Instrumentos VST3.xml");
}

// Archivos apartados (la búsqueda de "nuevos" no los vuelve a abrir)
StringArray savedBlacklist()
{
    StringArray ids;
    if (auto xml = XmlDocument::parse (savedListFile()))
        for (auto* e : xml->getChildWithTagNameIterator ("BLACKLISTED"))
            ids.add (e->getStringAttribute ("id"));
    return ids;
}

// Tipos guardados (todos, sin filtrar)
Array<PluginDescription> savedTypes()
{
    Array<PluginDescription> types;
    if (auto xml = XmlDocument::parse (savedListFile()))
        for (auto* e : xml->getChildIterator())
        {
            PluginDescription d;
            if (! e->hasTagName ("BLACKLISTED") && d.loadFromXml (*e))
                types.add (d);
        }
    return types;
}

bool sameFile (const String& a, const File& b)
{
    return File::createFileWithoutCheckingPath (a) == b;
}

PluginDescription describe (const String& name, const String& maker, bool instrument)
{
    PluginDescription d;
    d.name = name;
    d.descriptiveName = name;
    d.manufacturerName = maker;
    d.pluginFormatName = "VST3";
    d.isInstrument = instrument;
    d.category = instrument ? "Instrument|Synth" : "Fx";
    d.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/" + name + ".vst3";
    return d;
}
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "Uso: SmartKeysCatalogTests <carpeta con SmartKeysTestSynth.vst3>" << std::endl;
        return 2;
    }

    const auto pluginFolder = File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[1]));
    const auto userData = File::createTempFile ("smartkeys-catalog");
    userData.createDirectory();

   #if JUCE_WINDOWS
    _putenv_s ("SMARTKEYS_USER_DATA", userData.getFullPathName().toRawUTF8());
    _putenv_s ("VST3_PATH", pluginFolder.getFullPathName().toRawUTF8());
   #else
    setenv ("SMARTKEYS_USER_DATA", userData.getFullPathName().toRawUTF8(), 1);
    setenv ("VST3_PATH", pluginFolder.getFullPathName().toRawUTF8(), 1);
   #endif

    std::cout << "Carpeta de datos: " << userData.getFullPathName() << std::endl;
    check (PluginCatalog::getUserDataFolder() == userData, "SMARTKEYS_USER_DATA cambia la carpeta de datos");

    PluginDescription self;
    self.name = "SmartKeys";
    self.manufacturerName = "MIWN";
    self.pluginFormatName = "AudioUnit";
    self.fileOrIdentifier = "AudioUnit:Synths/aumu,Smk1,Jpau";
    check (PluginCatalog::isSmartKeysItself (self), "SmartKeys (AU) se reconoce a sí mismo");
    self.pluginFormatName = "VST3";
    self.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/SmartKeys.vst3";
    self.name = "SmartKeys";
    check (PluginCatalog::isSmartKeysItself (self), "SmartKeys (VST3) se reconoce a sí mismo");

    //==========================================================================
    std::cout << "\n-- 0. Qué se puede cargar en la ranura (sólo generadores de sonido)" << std::endl;
    {
        using C = PluginCatalog;
        check (C::isLoadableInstrument (describe ("Serum", "Xfer Records", true))
                 && C::isLoadableInstrument (describe ("PAPU", "Test", true))
                 && C::isLoadableInstrument (describe ("Kontakt 8", "Native Instruments", true))
                 && C::isLoadableInstrument (describe ("Vital", "Vital Audio", true)),
               "sintes y romplers (isInstrument) se pueden cargar");
        check (! C::isLoadableInstrument (describe ("Pro-Q 4", "FabFilter", false))
                 && ! C::isLoadableInstrument (describe ("ValhallaRoom", "Valhalla DSP, LLC", false))
                 && ! C::isLoadableInstrument (describe ("Pro-C 2", "FabFilter", false)),
               "EQ, reverb y compresor (efectos, isInstrument falso) no");
        check (! C::isLoadableInstrument (self), "SmartKeys no se carga dentro de sí mismo");
        check (C::isHostOrBrowser (describe ("Blue Cat's PatchWork Synth", "Blue Cat Audio", true))
                 && C::isHostOrBrowser (describe ("Unify", "PlugInGuru", true))
                 && C::isHostOrBrowser (describe ("Carla-Rack", "falkTX", true))
                 && C::isHostOrBrowser (describe ("Vienna Ensemble Pro", "VSL", true))
                 && C::isHostOrBrowser (describe ("FL Studio", "Image-Line", true))
                 && C::isHostOrBrowser (describe ("Loopcloud 7", "Loopmasters", true))
                 && C::isHostOrBrowser (describe ("Sample Manager", "ADSR Sounds", true)),
               "anfitriones de plugins, patchers y navegadores de samples se reconocen");
        check (! C::isLoadableInstrument (describe ("Blue Cat's PatchWork Synth", "Blue Cat Audio", true)),
               "aunque se declaren instrumento, no se ofrecen");
        check (! C::isHostOrBrowser (describe ("Dispatcher", "Acme", true))
                 && ! C::isHostOrBrowser (describe ("Sytrus", "Image-Line", true))
                 && ! C::isHostOrBrowser (describe ("Unifyer Pad", "Acme", true))
                 && ! C::isHostOrBrowser (describe ("Serum", "Xfer Records", true)),
               "palabras enteras: Dispatcher, Unifyer o Sytrus no son anfitriones");

        // 2.0.2: utilidades de ruteo y anfitriones que se declaran instrumento (con salidas)
        check (! C::isLoadableInstrument (describe ("AGridder Inst", "e47", true))
                 && ! C::isLoadableInstrument (describe ("AGridderInst", "e47", true))
                 && ! C::isLoadableInstrument (describe ("AudioGridderInst", "e47", true))
                 && ! C::isLoadableInstrument (describe ("AGridder", "e47", true)),
               "AGridder / AudioGridder (plugins por la red) no se ofrece");
        check (! C::isLoadableInstrument (describe ("Element", "Kushview", true))
                 && ! C::isLoadableInstrument (describe ("Element", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Komplete Kontrol", "Native Instruments", true)),
               "Element (anfitrión modular) y Komplete Kontrol no se ofrecen");
        check (! C::isLoadableInstrument (describe ("Patcher", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("MyPatcher", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("MIDI Monitor", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Control", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Spectrum Analyzer", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Signal Routing", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Gain Utility", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Effect Rack", "Acme", true))
                 && ! C::isLoadableInstrument (describe ("Synth FX", "Acme", true)),
               "nombres con Patcher, MIDI, Control, Analyzer, Routing, Utility, Effect o Fx no se ofrecen");
        auto fxCategory = describe ("Vocoder Pro", "Acme", true);
        fxCategory.category = "Fx|Instrument";
        check (! C::isLoadableInstrument (fxCategory), "categoría con Fx (\"Fx|Instrument\") no se ofrece");
        auto noOutput = describe ("Sidechain Gate", "Acme", true);
        noOutput.numInputChannels = 2;
        noOutput.numOutputChannels = 0;
        check (! C::isLoadableInstrument (noOutput), "un instrumento con entradas y sin salida de audio no se ofrece");
        check (C::isLoadableInstrument (describe ("Kontakt 7", "Native Instruments", true))
                 && C::isLoadableInstrument (describe ("Analog Lab V", "Arturia", true))
                 && C::isLoadableInstrument (describe ("Omnisphere", "Spectrasonics", true))
                 && C::isLoadableInstrument (describe ("Keyscape", "Spectrasonics", true))
                 && C::isLoadableInstrument (describe ("Pigments", "Arturia", true))
                 && C::isLoadableInstrument (describe ("Massive X", "Native Instruments", true))
                 && C::isLoadableInstrument (describe ("Electra2", "Tone2", true))
                 && C::isLoadableInstrument (describe ("Kaleidoscope", "Acme", true))
                 && C::isLoadableInstrument (describe ("Diva", "u-he", true)),
               "sintes, samplers y romplers siguen (Kontakt, Analog Lab, Omnisphere, Pigments, Diva…)");
    }

    //==========================================================================
   #if JUCE_MAC
    std::cout << "\n-- 1. Instrumentos Audio Unit del sistema (sin abrirlos)" << std::endl;
    {
        PluginCatalog catalog;
        const double t0 = Time::getMillisecondCounterHiRes();
        const auto list = catalog.getInstruments();
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        int au = 0;
        bool selfListed = false, allInstruments = true;
        for (const auto& d : list)
        {
            if (d.pluginFormatName != "AudioUnit")
                continue;
            ++au;
            selfListed |= PluginCatalog::isSmartKeysItself (d);
            allInstruments &= d.isInstrument && d.fileOrIdentifier.startsWith ("AudioUnit:Synths/aumu,");
            std::cout << "     " << d.manufacturerName << " · " << d.name << "  (" << d.fileOrIdentifier << ")" << std::endl;
        }
        std::cout << "     " << au << " instrumentos AU en " << ms << " ms" << std::endl;
        check (contains (list, "DLSMusicDevice", "AudioUnit"), "aparece DLSMusicDevice de Apple (un piano GM siempre disponible)");
        check (allInstruments, "sólo instrumentos (aumu), con el identificador de JUCE");
        check (! selfListed, "SmartKeys no se ofrece a sí mismo");
        check (ms < 2000.0, "la lista sale al instante (no se abre ningún plugin)");

        if (SystemStats::getEnvironmentVariable ("SMARTKEYS_EXPECT_TEST_AU", {}) == "1")
        {
            check (contains (list, "SmartKeysTestSynth", "AudioUnit"), "el instrumento de prueba instalado como AU aparece");
            check (! listedByName (list, "SmartKeysTestFx"), "el efecto de prueba instalado como AU (aufx) no aparece");
        }
    }
   #endif

    //==========================================================================
    std::cout << "\n-- 2. Búsqueda de instrumentos VST3" << std::endl;
    {
        PluginCatalog catalog;
        check (! catalog.hasEverScannedVst3(), "la primera vez no hay lista guardada");
        scanAndWait (catalog, false);
        const auto list = catalog.getInstruments();
        for (const auto& d : list)
            if (d.pluginFormatName == "VST3")
                std::cout << "     VST3: " << d.manufacturerName << " · " << d.name << "  (" << d.fileOrIdentifier << ")" << std::endl;
        check (! catalog.isScanning() && catalog.hasEverScannedVst3(), "la búsqueda termina");
        check (contains (list, "SmartKeysTestSynth", "VST3"), "encuentra el instrumento de prueba (VST3_PATH)");
        bool selfListed = false;
        for (const auto& d : list)
            selfListed |= PluginCatalog::isSmartKeysItself (d);
        check (! selfListed, "SmartKeys.vst3 no se ofrece a sí mismo");
        check (catalog.getLastScanNewInstruments() >= 1, "cuenta los instrumentos nuevos");

        // Sólo instrumentos: el efecto de prueba (VST3 "Fx|EQ") está en la misma carpeta
        const auto fx = pluginFolder.getChildFile ("SmartKeysTestFx.vst3");
        check (fx.isDirectory(), "el efecto de prueba está en la carpeta (SmartKeysTestFx.vst3)");
        check (! listedByName (list, "SmartKeysTestFx"), "el efecto no aparece en la lista: sólo instrumentos");
        bool fxBlacklisted = false;
        for (const auto& id : savedBlacklist())
            fxBlacklisted |= sameFile (id, fx);
        check (fxBlacklisted, "su archivo queda apartado: \"Buscar nuevos\" no lo vuelve a abrir");
        bool onlyInstruments = savedTypes().size() > 0;
        for (const auto& d : savedTypes())
            onlyInstruments &= PluginCatalog::isLoadableInstrument (d);
        check (onlyInstruments, "la lista guardada en disco sólo trae instrumentos");
    }
    {
        PluginCatalog catalog;   // otra instancia (otro proyecto, otro día)
        check (catalog.hasEverScannedVst3() && contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"),
               "la lista se guardó: la próxima vez sale sin buscar");
    }

    //==========================================================================
    std::cout << "\n-- 3. Un plugin que cerró el programa al examinarlo no se vuelve a examinar" << std::endl;
    {
        // Copia del instrumento con otro nombre, apuntada como "se estaba examinando"
        const auto crasher = pluginFolder.getChildFile ("Crasher.vst3");
        crasher.deleteRecursively();
        pluginFolder.getChildFile ("SmartKeysTestSynth.vst3").copyDirectoryTo (crasher);
        userData.getChildFile ("Buscando VST3.txt").replaceWithText (crasher.getFullPathName());

        PluginCatalog catalog;
        scanAndWait (catalog, true);
        bool crasherListed = false;
        for (const auto& d : catalog.getInstruments())
            crasherListed |= d.fileOrIdentifier == crasher.getFullPathName();
        check (! crasherListed && contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"),
               "el que cerró el programa queda fuera; los demás se encuentran");
        crasher.deleteRecursively();
        userData.getChildFile ("Buscando VST3.txt").deleteFile();
    }

    //==========================================================================
    std::cout << "\n-- 4. Carga protegida" << std::endl;
    {
        PluginDescription d;
        d.pluginFormatName = "VST3";
        d.name = "Broken";
        d.fileOrIdentifier = "/tmp/Broken.vst3";
        d.uniqueId = 1234;

        {
            PluginCatalog catalog;
            catalog.markLoadStarted (d);   // ... y el programa "se cierra" sin terminar
            PluginDescription ok = d;
            ok.fileOrIdentifier = "/tmp/Fine.vst3";
            catalog.markLoadStarted (ok);
            catalog.markLoadFinished (ok);
        }
        PluginCatalog next;
        check (next.crashedDuringLastLoad (d), "la carga que no terminó se recuerda al abrir otra vez");
        PluginDescription fine = d;
        fine.fileOrIdentifier = "/tmp/Fine.vst3";
        check (! next.crashedDuringLastLoad (fine), "la que terminó, no");
        next.forgetCrash (d);
        check (! next.crashedDuringLastLoad (d), "reintentarla a mano la olvida");
        check (! userData.getChildFile ("Cargando plugin.txt").existsAsFile(), "el apunte se limpia al abrir");
    }

    //==========================================================================
    std::cout << "\n-- 5. Presets de SmartKeys" << std::endl;
    {
        namespace presets = smartkeys::presets;
        auto items = presets::list();
        int factory = 0;
        for (const auto& i : items)
            factory += i.isFactory() ? 1 : 0;
        check (factory == presets::getNumFactoryPresets() && factory >= 6, "trae presets de fábrica");

        bool factoryOk = true;
        for (int i = 0; i < presets::getNumFactoryPresets(); ++i)
        {
            auto xml = presets::createFactory (i);
            factoryOk = factoryOk && xml != nullptr && xml->getNumChildElements() == 9;
        }
        check (factoryOk, "cada preset de fábrica trae sus 9 ajustes");

        check (presets::cleanName ("  Neo Soul / Keys: \"live\" ") == "Neo Soul - Keys- -live-", "el nombre se limpia para el archivo");

        XmlElement preset (presets::kRootTag);
        preset.setAttribute ("version", 1);
        auto* p = preset.createNewChildElement ("PARAM");
        p->setAttribute ("id", "rootKey");
        p->setAttribute ("value", 9.0);
        check (presets::save ("Mi preset", preset).wasOk(), "guardar un preset");
        check (presets::userPresetExists ("Mi preset"), "existe en la carpeta del usuario");

        items = presets::list();
        const presets::Item* mine = nullptr;
        for (const auto& i : items)
            if (! i.isFactory() && i.name == "Mi preset")
                mine = &i;
        check (mine != nullptr, "aparece en la lista");
        if (mine != nullptr)
        {
            auto loaded = presets::load (*mine);
            check (loaded != nullptr && loaded->getChildByName ("PARAM") != nullptr
                     && approximatelyEqual (loaded->getChildByName ("PARAM")->getDoubleAttribute ("value"), 9.0),
                   "se vuelve a cargar igual");
            check (presets::remove (*mine) && ! presets::userPresetExists ("Mi preset"), "borrar el preset");
        }
        check (! presets::save ("   ", preset).wasOk(), "sin nombre no se guarda");
    }


    //==========================================================================
    std::cout << "\n-- 6. La ranura por dentro: trozos y cambios con el audio corriendo" << std::endl;
    {
        PluginDescription synth;
        {
            PluginCatalog catalog;
            for (const auto& d : catalog.getInstruments())
                if (d.name == "SmartKeysTestSynth" && d.pluginFormatName == "VST3")
                    synth = d;
        }
        check (synth.fileOrIdentifier.isNotEmpty(), "el instrumento de prueba está en la lista");

        const auto log = userData.getChildFile ("synth.log");
       #if JUCE_WINDOWS
        _putenv_s ("SMARTKEYS_TESTSYNTH_LOG", log.getFullPathName().toRawUTF8());
       #else
        setenv ("SMARTKEYS_TESTSYNTH_LOG", log.getFullPathName().toRawUTF8(), 1);
       #endif

        auto pump = [] (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); };
        auto waitFor = [&] (smartkeys::HostedInstrument& slot, smartkeys::HostedInstrument::Status wanted)
        {
            const auto end = Time::getMillisecondCounterHiRes() + 10000.0;
            while (slot.getStatus() != wanted && Time::getMillisecondCounterHiRes() < end)
                pump (20);
            return slot.getStatus() == wanted;
        };
        auto logLines = [&] { pump (60); StringArray l; log.readLines (l); l.removeEmptyStrings(); return l; };

        smartkeys::HostedInstrument slot;
        slot.prepare (48000.0, 256);
        slot.load (synth);
        check (waitFor (slot, smartkeys::HostedInstrument::Status::Loaded), "se carga en la ranura");

        // Un bloque de 1024 con el instrumento preparado para 256: va en 4 trozos
        const int before = logLines().size();
        AudioBuffer<float> buffer (2, 1024);
        buffer.clear();
        MidiBuffer midi;
        midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 10);
        midi.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 700);
        const bool rendered = slot.process (buffer, 0, 2, midi, 1024, nullptr);
        const auto lines = logLines();
        int64 at60 = -1, at64 = -1;
        bool over = false;
        for (int i = before; i < lines.size(); ++i)
        {
            StringArray t;
            t.addTokens (lines[i], " ", {});
            if (t.size() >= 6 && t[1] == "EV" && t[3] == "144" && t[4] == "60") at60 = t[2].getLargeIntValue();
            if (t.size() >= 6 && t[1] == "EV" && t[3] == "144" && t[4] == "64") at64 = t[2].getLargeIntValue();
            over = over || lines[i].contains ("OVERBLOCK");
        }
        std::cout << "     notas en el instrumento: C4 en " << at60 << ", E4 en " << at64 << " (diferencia " << (at64 - at60) << ")" << std::endl;
        check (rendered && ! over, "un bloque de 1024 llega en trozos de 256 (nunca más grande que el preparado)");
        check (at60 >= 0 && at64 - at60 == 690, "y cada nota en su muestra exacta (+690)");
        check (buffer.getRMSLevel (0, 0, 1024) > 1.0e-4f, "y suena");

        // Hilo de audio sin parar + cargar / quitar / BYPASS / preparar en el hilo de mensajes
        std::atomic<bool> stop { false };
        std::atomic<long> blocks { 0 }, rendered2 { 0 }, nonFinite { 0 };
        std::thread audio ([&]
        {
            AudioBuffer<float> b (2, 512);
            MidiBuffer m;
            m.ensureSize (4096);
            std::mt19937 rng (3);
            while (! stop.load())
            {
                b.clear();
                m.clear();
                const int n = 32 + (int) (rng() % 481);
                if (rng() % 3 == 0)
                    m.addEvent (MidiMessage::noteOn (1, 48 + (int) (rng() % 30), (uint8) 90), (int) (rng() % (unsigned) n));
                if (rng() % 3 == 0)
                    m.addEvent (MidiMessage::noteOff (1, 48 + (int) (rng() % 30)), (int) (rng() % (unsigned) n));
                if (slot.process (b, 0, 2, m, n, nullptr))
                    ++rendered2;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        if (! std::isfinite (b.getSample (ch, i)))
                            ++nonFinite;
                ++blocks;
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        });

        std::mt19937 rng (9);
        int changes = 0;
        for (int i = 0; i < 60; ++i)
        {
            switch (rng() % 5)
            {
                case 0:  slot.unload(); break;
                case 1:  slot.load (synth); break;
                case 2:  slot.setBypassed (! slot.isBypassed()); break;
                case 3:  slot.prepare (48000.0, rng() % 2 == 0 ? 256 : 512); break;
                default: if (auto xml = slot.createStateXml()) slot.restoreState (xml.get()); break;
            }
            ++changes;
            pump (15 + (int) (rng() % 30));
        }
        slot.setBypassed (false);
        slot.load (synth);
        waitFor (slot, smartkeys::HostedInstrument::Status::Loaded);
        pump (200);
        stop = true;
        audio.join();
        std::cout << "     " << blocks.load() << " bloques de audio (" << rendered2.load() << " con sonido) mientras el hilo de mensajes hizo "
                  << changes << " cambios" << std::endl;
        check (blocks.load() > 200 && rendered2.load() > 50, "el audio siguió sonando entre cambio y cambio");
        check (nonFinite.load() == 0, "nunca salió NaN / infinito");
        slot.shutdown();
        AudioBuffer<float> after (2, 64);
        MidiBuffer none;
        check (! slot.process (after, 0, 2, none, 64, nullptr), "al cerrar, el instrumento se destruye y el audio ya no lo toca");
    }

    //==========================================================================
    std::cout << "\n-- 7. Sólo instrumentos: lo apartado no se vuelve a abrir; las listas antiguas se limpian" << std::endl;
    {
        const auto fx = pluginFolder.getChildFile ("SmartKeysTestFx.vst3");
        const auto info = fx.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("moduleinfo.json");
        const auto original = info.loadFileAsString();
        check (original.contains ("\"Fx\"") && ! original.contains ("\"Instrument\""),
               "el efecto de prueba se describe como efecto (moduleinfo.json: Fx)");

        auto fxBlacklisted = [&]
        {
            for (const auto& id : savedBlacklist())
                if (sameFile (id, fx))
                    return true;
            return false;
        };

        {
            PluginCatalog catalog;   // la lista de antes: el efecto está apartado
            check (fxBlacklisted() && ! listedByName (catalog.getInstruments(), "SmartKeysTestFx"),
                   "al abrir, el efecto sigue fuera y apartado");

            // El archivo cambia (p. ej. una actualización) y ahora se declararía instrumento:
            // "Buscar nuevos" ni lo mira (no se vuelve a abrir un archivo apartado)
            info.replaceWithText (original.replace ("\"Fx\"", "\"Instrument\""), false, false, nullptr);
            scanAndWait (catalog, false);
            check (! listedByName (catalog.getInstruments(), "SmartKeysTestFx") && fxBlacklisted(),
                   "\"Buscar nuevos\" no vuelve a abrir el archivo apartado");

            // "Volver a buscar todos" revisa todo otra vez: ahora sí se declara instrumento
            scanAndWait (catalog, true);
            check (listedByName (catalog.getInstruments(), "SmartKeysTestFx") && ! fxBlacklisted(),
                   "\"Volver a buscar todos\" lo revisa otra vez (declarado instrumento, entra)");

            // Y vuelve a ser un efecto: fuera otra vez
            info.replaceWithText (original, false, false, nullptr);
            scanAndWait (catalog, true);
            check (! listedByName (catalog.getInstruments(), "SmartKeysTestFx") && fxBlacklisted()
                     && contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"),
                   "efecto otra vez: fuera y apartado; el instrumento sigue");
        }
        check (info.loadFileAsString() == original, "el efecto de prueba queda como estaba");

        // Una lista guardada por una versión anterior (2.0.0 guardaba también efectos)
        if (auto xml = XmlDocument::parse (savedListFile()))
        {
            xml->addChildElement (describe ("Old Reverb", "Acme", false).createXml().release());
            xml->addChildElement (describe ("Old EQ", "Acme", false).createXml().release());
            xml->addChildElement (describe ("Blue Cat's PatchWork Synth", "Blue Cat Audio", true).createXml().release());
            xml->writeTo (savedListFile());
        }
        check (savedTypes().size() >= 4, "lista antigua con dos efectos y un anfitrión de plugins");
        {
            PluginCatalog catalog;
            const auto list = catalog.getInstruments();
            check (! listedByName (list, "Old Reverb") && ! listedByName (list, "Old EQ")
                     && ! listedByName (list, "Blue Cat's PatchWork Synth")
                     && contains (list, "SmartKeysTestSynth", "VST3"),
                   "al abrir no se ofrecen (el instrumento sigue)");
        }
        bool clean = true;
        for (const auto& d : savedTypes())
            clean &= PluginCatalog::isLoadableInstrument (d);
        check (clean && contains (savedTypes(), "SmartKeysTestSynth", "VST3"), "y la lista se guarda limpia en disco");
    }

    //==========================================================================
    std::cout << "\n-- 8. Utilidades que se declaran instrumento (AGridder, Element) y sin salida de audio" << std::endl;
    {
        // Una copia del instrumento de prueba que se presenta como "AGridder Inst" (como
        // AudioGridder: se declara instrumento, con salidas, y no suena por sí mismo)
        const auto synth = pluginFolder.getChildFile ("SmartKeysTestSynth.vst3");
        const auto gridder = pluginFolder.getChildFile ("AGridder Inst.vst3");
        gridder.deleteRecursively();
        const bool copied = synth.copyDirectoryTo (gridder);
        const auto info = gridder.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("moduleinfo.json");
        const auto original = info.loadFileAsString();
        const bool renamed = copied && original.contains ("\"SmartKeysTestSynth\"")
                               && info.replaceWithText (original.replace ("\"SmartKeysTestSynth\"", "\"AGridder Inst\""),
                                                        false, false, nullptr);
        check (renamed && original.contains ("\"Instrument\""),
               "copia del instrumento de prueba que se llama \"AGridder Inst\" (se declara instrumento)");

        auto gridderBlacklisted = [&]
        {
            for (const auto& id : savedBlacklist())
                if (sameFile (id, gridder))
                    return true;
            return false;
        };

        {
            PluginCatalog catalog;
            scanAndWait (catalog, false);
            const auto list = catalog.getInstruments();
            check (! listedByName (list, "AGridder Inst") && contains (list, "SmartKeysTestSynth", "VST3"),
                   "la búsqueda no ofrece AGridder (el instrumento de verdad sigue)");
            check (gridderBlacklisted(), "y aparta su archivo (\"Buscar nuevos\" no lo vuelve a abrir)");

            // Ahora se llama "Element" (anfitrión modular de Kushview): tampoco
            info.replaceWithText (original.replace ("\"SmartKeysTestSynth\"", "\"Element\""), false, false, nullptr);
            scanAndWait (catalog, true);
            check (! listedByName (catalog.getInstruments(), "Element") && contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3")
                     && gridderBlacklisted(),
                   "\"Volver a buscar todos\": Element tampoco se ofrece");
        }
        gridder.deleteRecursively();

        // Un instrumento que al cargarlo resulta no tener salida de audio sale de la lista (y se
        // recuerda); "Volver a buscar todos" le da otra oportunidad
        {
            PluginCatalog catalog;
            scanAndWait (catalog, true);
            PluginDescription synthDescription;
            for (const auto& d : catalog.getInstruments())
                if (d.name == "SmartKeysTestSynth" && d.pluginFormatName == "VST3")
                    synthDescription = d;
            check (synthDescription.fileOrIdentifier.isNotEmpty() && ! catalog.isExcluded (synthDescription),
                   "el instrumento de prueba está en la lista");
            catalog.excludeNonGenerator (synthDescription);
            check (catalog.isExcluded (synthDescription) && ! contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"),
                   "si al cargarlo no tuviera salida de audio, sale de la lista");
        }
        {
            PluginCatalog catalog;   // otra instancia: lo recuerda
            check (! contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"), "y lo recuerda al abrir otra vez");
            scanAndWait (catalog, true);
            check (contains (catalog.getInstruments(), "SmartKeysTestSynth", "VST3"),
                   "\"Volver a buscar todos\" le da otra oportunidad");
        }
    }

    userData.deleteRecursively();
    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    return numFailures == 0 ? 0 : 1;
}
