/*
  ==============================================================================

    CatalogTests.cpp
    SmartKeys — prueba del catálogo de instrumentos de la ranura (2.0) y de los
    presets propios:

      1. Audio Unit (macOS): la lista de instrumentos del sistema sale sin abrir
         ningún plugin (DLSMusicDevice de Apple siempre está; el instrumento de
         prueba si se instaló) y SmartKeys no se ofrece a sí mismo.
      2. VST3: la búsqueda (VST3_PATH) encuentra el instrumento de prueba, se
         salta SmartKeys.vst3, se guarda en disco y la próxima vez ya se sabe.
      3. Un plugin que cerró el programa al examinarlo no se vuelve a examinar.
      4. Carga protegida: una carga que no terminó (el programa se cerró) se
         recuerda al abrir otra vez.
      5. Presets: los de fábrica, guardar, listar, cargar y borrar los del usuario.
      6. La ranura por dentro (HostedInstrument, sin DAW): un bloque más grande
         que el preparado llega al instrumento en trozos con cada nota en su
         muestra; y un hilo de audio procesa sin parar mientras el hilo de
         mensajes carga, quita, pone en BYPASS y vuelve a preparar el instrumento.

    Uso: SmartKeysCatalogTests <carpeta con SmartKeysTestSynth.vst3 [y SmartKeys.vst3]>
         (SMARTKEYS_EXPECT_TEST_AU=1: el AU de prueba debe estar registrado)

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

void scanAndWait (PluginCatalog& catalog, bool everything)
{
    catalog.startVst3Scan (everything);
    const auto end = Time::getMillisecondCounterHiRes() + 60000.0;
    while (catalog.isScanning() && Time::getMillisecondCounterHiRes() < end)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
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
            check (contains (list, "SmartKeysTestSynth", "AudioUnit"), "el instrumento de prueba instalado como AU aparece");
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

    userData.deleteRecursively();
    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    return numFailures == 0 ? 0 : 1;
}
