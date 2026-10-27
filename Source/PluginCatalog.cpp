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
}

//==============================================================================
PluginCatalog::PluginCatalog()
{
    juce::addDefaultFormatsToManager (formats);

    const auto folder = getUserDataFolder();
    folder.createDirectory();

    // Instrumentos VST3 de la última búsqueda
    if (auto xml = juce::XmlDocument::parse (folder.getChildFile (kVst3ListFile)))
    {
        vst3List.recreateFromXml (*xml);
        everScanned = true;
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
        for (const auto& d : listAudioUnitInstruments())
            if (! isSmartKeysItself (d))
                result.add (d);
   #endif

    for (const auto& d : vst3List.getTypes())
        if (d.isInstrument && d.pluginFormatName == "VST3" && ! isSmartKeysItself (d))
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
        n += (d.isInstrument && ! isSmartKeysItself (d)) ? 1 : 0;
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
