/*
  ==============================================================================

    PluginCatalog.cpp
    SmartKeys (MIWN) — Catálogo de instrumentos para la ranura de SmartKeys.

  ==============================================================================
*/

#include "PluginCatalog.h"

namespace smartkeys
{

namespace
{
    constexpr const char* kVst3ListFile   = "Instrumentos VST3.xml";
    constexpr const char* kScanCheckFile  = "Buscando VST3.txt";        // dead man's pedal de la búsqueda
    constexpr const char* kLoadCheckFile  = "Cargando plugin.txt";      // cargas en curso (carga protegida)
    constexpr const char* kExcludedFile   = "Instrumentos excluidos.txt";   // al cargarlos no tenían salida de audio

    // SÓLO GENERADORES DE SONIDO. Un plugin puede declararse instrumento y no sonar por sí
    // mismo: anfitriones de plugins (AudioGridder, Element, PatchWork…), patchers, herramientas
    // MIDI, analizadores, utilidades. Se reconocen por su nombre y su categoría:
    //
    //  - palabras enteras en el nombre o en la categoría ("Dispatch Synth" sí; "MIDI Router" no)
    constexpr const char* kNotAGeneratorWords[] = {
        "fx", "effect", "effects", "analyzer", "analyser", "analysis", "spectrum", "meter", "scope",
        "routing", "router", "network", "midi", "utility", "utilities", "tool", "tools",
        "control", "controller", "external", "patcher", "element", "host", "wrapper", "bridge",
    };

    //  - trozos de nombres pegados: AGridder / AudioGridder, PatchWork, MyPatcher
    constexpr const char* kNotAGeneratorParts[] = { "gridder", "patcher", "patchwork" };

    //  - anfitriones de plugins, patchers y navegadores de samples (nombre y fabricante)
    constexpr const char* kHostsAndBrowsers[] = {
        "patchwork",          // Blue Cat's PatchWork
        "bidule",             // Plogue Bidule
        "unify",              // PlugInGuru Unify
        "carla",              // Carla (KXStudio)
        "metaplugin",         // DDMF Metaplugin
        "gig performer",
        "vienna ensemble",    // Vienna Ensemble Pro (servidor de plugins)
        "fl studio",          // FL Studio como plugin (un DAW entero)
        "kushview",           // Element (anfitrión modular de plugins)
        "audiogridder",       // AGridder (plugins por la red)
        "komplete kontrol",   // anfitrión y navegador de Native Instruments
        "sample manager",     // ADSR Sample Manager
        "loopcloud",
        "splice bridge",
    };

    // SÓLO INSTRUMENTOS QUE TOCAN ACORDES (2.0.3). SmartKeys les manda progresiones y armonías:
    // fuera las baterías y percusiones (cajas de ritmos, samplers de batería) y los reproductores
    // de loops, frases y samples ya hechos. Kontakt y los demás samplers siguen (cargan pianos,
    // cuerdas…), igual que la percusión afinada (marimba, vibráfono…), que sí toca acordes.
    //
    //  - batería y percusión: palabras enteras en el nombre o en la categoría (VST3:
    //    "Instrument|Drum"; Audio Unit: su etiqueta "Drums", ver HostingMac.mm)
    constexpr const char* kRhythmWords[] = {
        "drum", "drums", "drummer", "drumkit", "drumkits", "percussion", "percussions", "perc", "percs",
        "kick", "kicks", "snare", "snares", "hihat", "hihats", "cymbal", "cymbals", "clap", "claps",
        "conga", "congas", "bongo", "bongos", "tabla", "cajon", "taiko", "djembe", "timbales",
        "beat", "beats", "groove", "grooves", "groovebox",
        "808", "909", "606", "707", "727",   // cajas de ritmos de Roland (TR-808…)
    };

    //    y en el fabricante ("GetGood Drums", "Steven Slate Drums")
    constexpr const char* kRhythmMakerWords[] = { "drum", "drums", "drummer", "percussion" };

    //  - trozos de nombres pegados: EZdrummer, DrumGizmo, MTPowerDrumKit, Kick3, Beatmaker…
    constexpr const char* kRhythmParts[] = {
        "drum", "percussion", "snare", "hihat", "kick", "beatmaker", "beatbox", "breakbeat",
        "heartbeat", "groovebox", "grooveagent", "ssdsampler",
    };

    //  - loops, frases y samples ya hechos: palabras enteras en el nombre o en la categoría
    //    ("Sampler" no: Kontakt, HALion o Decent Sampler cargan instrumentos que tocan acordes)
    //    y los que trocean un sample (cada tecla, un trozo: no tocan acordes)
    constexpr const char* kLoopWords[] = {
        "loop", "loops", "looper", "phrase", "phrases", "breaks", "breakbeat", "breakbeats",
        "oneshot", "oneshots", "sample", "samples", "slicer", "construction", "sfx", "foley",
        "chop", "chops", "chopper", "choppers", "slice", "slices",
    };
    constexpr const char* kLoopParts[] = { "loopmash", "breaktweaker", "chopbeast" };

    //  - los conocidos cuyo nombre no lo dice (con su fabricante si el nombre solo es ambiguo)
    struct KnownPlugin
    {
        const char* name;    // palabra entera del nombre
        const char* maker;   // palabra entera del fabricante (nullptr: cualquiera)
    };
    constexpr KnownPlugin kKnownRhythmAndLoops[] = {
        { "battery", nullptr },          // Native Instruments Battery
        { "xo", nullptr },               // XLN Audio XO
        { "microtonic", nullptr },       // Sonic Charge Microtonic
        { "nerve", nullptr },            // Xfer Records Nerve
        { "sitala", nullptr },
        { "bfd", nullptr }, { "bfd2", nullptr }, { "bfd3", nullptr },
        { "geist", nullptr }, { "geist2", nullptr },
        { "tremor", nullptr },           // Native Instruments Tremor
        { "punchbox", nullptr },         // D16 Group
        { "nepheton", nullptr },         // D16 Group (808)
        { "nithonat", nullptr },         // D16 Group (606)
        { "maschine", nullptr },         // groovebox de Native Instruments
        { "mpc", nullptr },              // groovebox de Akai
        { "ssd5", nullptr },             // Steven Slate Drums
        { "bm", "ujam" },                // UJAM Beatmaker (BM-DOPE, BM-EPIC…)
        { "vd", "ujam" },                // UJAM Virtual Drummer (VD-SOLID…)
        { "transfuser", nullptr },       // AIR Transfuser (loops y ritmos)
        { "spark", "arturia" },          // Arturia Spark (caja de ritmos)
        { "atlas", "algonaut" },         // Algonaut Atlas (samples de batería)
        { "backbone", "steinberg" },     // Steinberg Backbone (diseño de baterías)
        { "trigger", "slate" },          // Slate Trigger (reemplazo de batería)
        { "strike", "air" },             // AIR Strike (batería)
        { "boom", "air" },               // AIR Boom (caja de ritmos)
        { "punch", "papen" },            // Rob Papen Punch (batería)
        { "egoist", "sugar" },           // Sugar Bytes Egoist (loops troceados)
        { "adm", "audiorealism" },       // AudioRealism Drum Machine
        { "stylus", "spectrasonics" },   // Stylus RMX (grooves)
        { "arcade", "output" },          // Output Arcade (loops)
    };

    //  - la percusión afinada sí toca acordes: con una de estas palabras, "percussion" no basta
    constexpr const char* kTunedPercussionWords[] = {
        "tuned", "marimba", "vibraphone", "vibes", "glockenspiel", "glock", "celesta", "celeste",
        "xylophone", "kalimba", "handpan", "steelpan", "mallet", "mallets",
    };

    //  - etiquetas de Audio Unit de batería o percusión, también traducidas por macOS
    constexpr const char* kDrumTagWords[] = {
        "drum", "drums", "percussion", "percussions",
        "bateria", "percusion", "percussao", "batterie", "batteria", "percussioni", "schlagzeug", "perkussion",
    };

    // "Batería" -> "bateria" (las etiquetas traducidas se comparan sin acentos)
    juce::String withoutAccents (const juce::String& text)
    {
        return text.toLowerCase().replaceCharacters (juce::String (juce::CharPointer_UTF8 ("\xc3\xa1\xc3\xa0\xc3\xa2\xc3\xa4\xc3\xa3"
                                                                                            "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab"
                                                                                            "\xc3\xad\xc3\xac\xc3\xae\xc3\xaf"
                                                                                            "\xc3\xb3\xc3\xb2\xc3\xb4\xc3\xb6\xc3\xb5"
                                                                                            "\xc3\xba\xc3\xb9\xc3\xbb\xc3\xbc"
                                                                                            "\xc3\xa7\xc3\xb1")),
                                                     "aaaaaeeeeiiiiooooouuuucn");
    }

    // Sólo letras y números, en minúsculas: "EZdrummer 3" -> "ezdrummer3"
    juce::String lettersOf (const juce::String& text)
    {
        juce::String letters;
        for (auto c : text.toLowerCase())
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                letters << juce::String::charToString (c);
        return letters;
    }

    // " palabra1 palabra2 " en minúsculas: todo lo que no es letra o número separa
    juce::String wordsOf (const juce::String& text)
    {
        juce::String words (" ");
        for (auto c : text.toLowerCase())
            words << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String (" "));
        return words + " ";
    }

    bool containsWord (const juce::String& words, const char* word)
    {
        return words.contains (" " + juce::String (word) + " ");
    }

    // ¿Trae moduleinfo.json? Entonces JUCE lo describe sin abrirlo y no sabe sus canales
    bool hasModuleInfo (const juce::String& fileOrIdentifier)
    {
        const auto bundle = juce::File::createFileWithoutCheckingPath (fileOrIdentifier);
        const auto contents = bundle.getChildFile ("Contents");
        return contents.getChildFile ("Resources").getChildFile ("moduleinfo.json").existsAsFile()
            || contents.getChildFile ("moduleinfo.json").existsAsFile();
    }

    // La búsqueda de VST3 sólo apunta generadores de sonido. Un archivo que no trae ninguno
    // (sólo efectos, anfitriones, herramientas MIDI) devuelve "no apto" (false): JUCE lo aparta
    // y las búsquedas de "nuevos" ya no lo abren ("Volver a buscar todos" lo revisa otra vez).
    // Un archivo que no se pudo abrir sigue como siempre (se reintenta la próxima vez).
    class InstrumentsOnlyScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile (found, fileOrIdentifier);

            // Sin moduleinfo.json, JUCE abrió el plugin y sus canales son los de verdad: un
            // "instrumento" sin salidas de audio no suena
            const bool channelsKnown = format.getName() != "VST3" || ! hasModuleInfo (fileOrIdentifier);

            for (auto* d : found)
                if (d != nullptr && PluginCatalog::isLoadableInstrument (*d)
                      && ! (channelsKnown && d->numOutputChannels <= 0))
                    result.add (new juce::PluginDescription (*d));

            return ! result.isEmpty() || found.isEmpty();
        }
    };
}

//==============================================================================
PluginCatalog::PluginCatalog()
{
    juce::addDefaultFormatsToManager (formats);
    vst3List.setCustomScanner (std::make_unique<InstrumentsOnlyScanner>());

    const auto folder = getUserDataFolder();
    folder.createDirectory();

    // Instrumentos VST3 de la última búsqueda (si una versión anterior guardó también efectos, fuera)
    if (auto xml = juce::XmlDocument::parse (folder.getChildFile (kVst3ListFile)))
    {
        vst3List.recreateFromXml (*xml);
        everScanned = true;
        if (pruneNonInstruments())
            saveVst3List();
    }

    // Lo que se estaba cargando cuando el programa se cerró la última vez
    const auto markers = folder.getChildFile (kLoadCheckFile);
    if (markers.existsAsFile())
    {
        markers.readLines (crashedLastTime);
        crashedLastTime.removeEmptyStrings();
        crashedLastTime.removeDuplicates (false);
        markers.deleteFile();
    }

    // Instrumentos que al cargarlos resultaron no tener salida de audio
    folder.getChildFile (kExcludedFile).readLines (excluded);
    excluded.removeEmptyStrings();
    excluded.removeDuplicates (false);
}

PluginCatalog::~PluginCatalog()
{
    stopTimer();
    if (scanner != nullptr)
        finishScan();
}

juce::File PluginCatalog::getUserDataFolder()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("SMARTKEYS_USER_DATA", {});
    if (custom.isNotEmpty())
        return juce::File (custom);

   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("MIWN").getChildFile ("SmartKeys");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("MIWN").getChildFile ("SmartKeys");
   #endif
}

bool PluginCatalog::isSmartKeysItself (const juce::PluginDescription& d)
{
    if (d.fileOrIdentifier.containsIgnoreCase (",Smk1,Jpau"))
        return true;
    if (juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier).getFileName().equalsIgnoreCase ("SmartKeys.vst3"))
        return true;
    return d.name.equalsIgnoreCase ("SmartKeys") && d.manufacturerName.equalsIgnoreCase ("MIWN");
}

bool PluginCatalog::isHostOrBrowser (const juce::PluginDescription& d)
{
    // Palabras enteras del nombre y del fabricante ("Unifyer" no es "Unify")
    const auto words = wordsOf (d.name + " " + d.descriptiveName + " " + d.manufacturerName);
    for (auto* phrase : kHostsAndBrowsers)
        if (containsWord (words, phrase))
            return true;
    return false;
}

bool PluginCatalog::hasNonGeneratorName (const juce::PluginDescription& d)
{
    // Palabras enteras del nombre y de la categoría (VST3: "Instrument|Synth", "Fx|EQ"…)
    const auto words = wordsOf (d.name + " " + d.descriptiveName + " " + d.category);
    for (auto* word : kNotAGeneratorWords)
        if (containsWord (words, word))
            return true;

    // Nombres pegados ("AGridder", "AudioGridderInst"): sólo letras y números
    const auto letters = lettersOf (d.name + " " + d.descriptiveName);
    for (auto* part : kNotAGeneratorParts)
        if (letters.contains (part))
            return true;
    return false;
}

bool PluginCatalog::isDrumTag (const juce::String& tag)
{
    const auto words = wordsOf (withoutAccents (tag));
    for (auto* word : kDrumTagWords)
        if (containsWord (words, word))
            return true;
    return false;
}

bool PluginCatalog::isDrumsOrLoops (const juce::PluginDescription& d)
{
    const auto nameWords = wordsOf (d.name + " " + d.descriptiveName);
    const auto words = wordsOf (d.name + " " + d.descriptiveName + " " + d.category);
    const auto makerWords = wordsOf (d.manufacturerName);
    const auto letters = lettersOf (d.name + " " + d.descriptiveName);

    // Loops, frases y samples ya hechos
    for (auto* word : kLoopWords)
        if (containsWord (words, word))
            return true;
    for (auto* part : kLoopParts)
        if (letters.contains (part))
            return true;

    // Los conocidos (Battery, XO, Arturia Spark, Output Arcade…)
    for (const auto& known : kKnownRhythmAndLoops)
        if (containsWord (nameWords, known.name) && (known.maker == nullptr || containsWord (makerWords, known.maker)))
            return true;

    // Batería y percusión (la percusión afinada sí toca acordes)
    for (auto* word : kTunedPercussionWords)
        if (containsWord (nameWords, word))
            return false;

    for (auto* word : kRhythmWords)
        if (containsWord (words, word))
            return true;
    for (auto* word : kRhythmMakerWords)
        if (containsWord (makerWords, word))
            return true;
    for (auto* part : kRhythmParts)
        if (letters.contains (part))
            return true;
    return false;
}

bool PluginCatalog::isLoadableInstrument (const juce::PluginDescription& d)
{
    if (! d.isInstrument || isSmartKeysItself (d) || isHostOrBrowser (d) || hasNonGeneratorName (d) || isDrumsOrLoops (d))
        return false;

    // Si se saben sus canales (tiene entradas declaradas) y no tiene salidas, no suena
    return ! (d.numInputChannels > 0 && d.numOutputChannels <= 0);
}

//==============================================================================
// Instrumentos que al cargarlos resultaron no tener salida de audio (VST3 descritos por su
// moduleinfo.json sin abrirlos: sus canales sólo se saben al crearlos)
void PluginCatalog::excludeNonGenerator (const juce::PluginDescription& d)
{
    const auto key = loadKey (d);
    if (excluded.contains (key))
        return;

    excluded.add (key);
    saveExcluded();
    changeCounter.fetch_add (1, std::memory_order_release);
}

bool PluginCatalog::isExcluded (const juce::PluginDescription& d) const
{
    return excluded.contains (loadKey (d));
}

void PluginCatalog::saveExcluded()
{
    const auto folder = getUserDataFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile (kExcludedFile);
    if (excluded.isEmpty())
        file.deleteFile();
    else
        file.replaceWithText (excluded.joinIntoString ("\n"), false, false);
}

bool PluginCatalog::pruneNonInstruments()
{
    bool changed = false;
    for (const auto& d : vst3List.getTypes())
    {
        if (! isLoadableInstrument (d))
        {
            vst3List.removeType (d);
            changed = true;
        }
    }
    return changed;
}

juce::String PluginCatalog::getFormatDisplayName (const juce::PluginDescription& d)
{
    return d.pluginFormatName == "AudioUnit" ? juce::String ("Audio Unit") : d.pluginFormatName;
}

juce::AudioPluginFormat* PluginCatalog::findFormat (const juce::String& name) const
{
    for (auto* f : formats.getFormats())
        if (f->getName() == name)
            return f;
    return nullptr;
}

//==============================================================================
juce::Array<juce::PluginDescription> PluginCatalog::getInstruments()
{
    juce::Array<juce::PluginDescription> result;

   #if JUCE_MAC
    if (findFormat ("AudioUnit") != nullptr)
        for (const auto& d : listAudioUnitInstruments())   // sólo kAudioUnitType_MusicDevice
            if (isLoadableInstrument (d) && ! isExcluded (d))
                result.add (d);
   #endif

    for (const auto& d : vst3List.getTypes())
        if (d.pluginFormatName == "VST3" && isLoadableInstrument (d) && ! isExcluded (d))
            result.add (d);

    std::sort (result.begin(), result.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        if (a.pluginFormatName != b.pluginFormatName)
            return a.pluginFormatName == "AudioUnit";   // Audio Unit primero (macOS)
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        if (m != 0)
            return m < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    // Un mismo instrumento VST3 puede aparecer dos veces (dos carpetas): uno solo
    for (int i = result.size(); --i > 0;)
        for (int j = 0; j < i; ++j)
            if (result.getReference (i).isDuplicateOf (result.getReference (j)))
            {
                result.remove (i);
                break;
            }

    return result;
}

int PluginCatalog::countVst3Instruments() const
{
    int n = 0;
    for (const auto& d : vst3List.getTypes())
        n += isLoadableInstrument (d) && ! isExcluded (d) ? 1 : 0;
    return n;
}

//==============================================================================
void PluginCatalog::startVst3Scan (bool rescanEverything)
{
    if (scanner != nullptr)
        return;

    auto* vst3 = findFormat ("VST3");
    if (vst3 == nullptr)
        return;

    if (rescanEverything)
    {
        vst3List.clear();
        vst3List.clearBlacklistedFiles();
        excluded.clear();   // otra oportunidad (una versión nueva del plugin puede tener salida de audio)
        saveExcluded();
    }

    instrumentsBeforeScan = countVst3Instruments();
    scanEverything = rescanEverything;

    // SmartKeys no se examina a sí mismo
    auto files = vst3->searchPathsForPlugins (vst3->getDefaultLocationsToSearch(), true, false);
    for (int i = files.size(); --i >= 0;)
        if (juce::File::createFileWithoutCheckingPath (files[i]).getFileName().equalsIgnoreCase ("SmartKeys.vst3"))
            files.remove (i);

    scanner = std::make_unique<juce::PluginDirectoryScanner> (vst3List, *vst3, juce::FileSearchPath(), true,
                                                              getUserDataFolder().getChildFile (kScanCheckFile), false);
    scanner->setFilesOrIdentifiersToScan (files);
    scanningName = {};

    changeCounter.fetch_add (1, std::memory_order_release);
    startTimer (files.isEmpty() ? 1 : 15);
}

float PluginCatalog::getScanProgress() const noexcept
{
    return scanner != nullptr ? scanner->getProgress() : 1.0f;
}

void PluginCatalog::timerCallback()
{
    if (scanner == nullptr)
    {
        stopTimer();
        return;
    }

    // Un archivo por tic: la interfaz sigue respondiendo entre uno y otro
    scanningName = scanner->getNextPluginFileThatWillBeScanned();
    juce::String name;
    const bool more = scanner->scanNextFile (! scanEverything, name);
    changeCounter.fetch_add (1, std::memory_order_release);

    if (! more)
        finishScan();
}

void PluginCatalog::finishScan()
{
    stopTimer();
    scanner.reset();
    scanningName = {};
    everScanned = true;
    lastScanNew = juce::jmax (0, countVst3Instruments() - instrumentsBeforeScan);
    saveVst3List();
    changeCounter.fetch_add (1, std::memory_order_release);
}

void PluginCatalog::saveVst3List()
{
    if (auto xml = vst3List.createXml())
    {
        const auto folder = getUserDataFolder();
        folder.createDirectory();
        xml->writeTo (folder.getChildFile (kVst3ListFile));
    }
}

//==============================================================================
// Carga protegida: cada carga en curso se apunta en un archivo antes de crear
// el plugin y se borra al terminar (bien o mal). Si el programa se cierra a
// mitad, el apunte se queda y la próxima vez se sabe.
juce::String PluginCatalog::loadKey (const juce::PluginDescription& d)
{
    return d.pluginFormatName + "|" + d.fileOrIdentifier + "|" + juce::String (d.uniqueId);
}

void PluginCatalog::writeLoadMarkers()
{
    const auto folder = getUserDataFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile (kLoadCheckFile);
    if (loadsInProgress.isEmpty())
        file.deleteFile();
    else
        file.replaceWithText (loadsInProgress.joinIntoString ("\n"), false, false);
}

void PluginCatalog::markLoadStarted (const juce::PluginDescription& d)
{
    loadsInProgress.add (loadKey (d));
    writeLoadMarkers();
}

void PluginCatalog::markLoadFinished (const juce::PluginDescription& d)
{
    const int i = loadsInProgress.indexOf (loadKey (d));
    if (i >= 0)
    {
        loadsInProgress.remove (i);
        writeLoadMarkers();
    }
}

bool PluginCatalog::crashedDuringLastLoad (const juce::PluginDescription& d) const
{
    return crashedLastTime.contains (loadKey (d));
}

void PluginCatalog::forgetCrash (const juce::PluginDescription& d)
{
    crashedLastTime.removeString (loadKey (d));
}

} // namespace smartkeys
