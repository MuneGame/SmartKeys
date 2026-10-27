/*
  ==============================================================================

    Presets.cpp
    SmartKeys (MIWN) — Presets propios de SmartKeys.

  ==============================================================================
*/

#include "Presets.h"
#include "PluginCatalog.h"
#include "ScaleChordEngine.h"

namespace smartkeys::presets
{

namespace
{
    struct Factory
    {
        const char* name;
        int root;               // 0 = C
        const char* scale;      // nombre en la base de escalas
        int mode;               // 0 Smart, 1 Single, 2 Complex
        int chordExt;           // 0 Triad, 1 +8va, 2 7th
        int complexVar;         // 0 Lush, 1 Neo-Soul, 2 Cinematic
        float strumMs;
        int strumDir;           // 0 ASC, 1 DESC
        bool micOn;
        float gateDb;
    };

    const Factory kFactory[] =
    {
        { "Pop · C Major",            0, "Major",                 0, 0, 0,   0.0f, 0, false, -48.0f },
        { "Ballad · A Minor 7th",     9, "Minor",                 0, 2, 0,  40.0f, 0, false, -48.0f },
        { "Neo-Soul · D Dorian",      2, "Dorian",                2, 0, 1,  25.0f, 0, false, -48.0f },
        { "Lush 7/9 · F Major",       5, "Major",                 2, 0, 0,  15.0f, 0, false, -48.0f },
        { "Cinematic · E Minor",      4, "Minor",                 2, 0, 2,  90.0f, 0, false, -48.0f },
        { "Harp · G Major (DESC)",    7, "Major",                 0, 1, 0, 120.0f, 1, false, -48.0f },
        { "Maqam Hijaz · D",          2, "Maqam Hijaz",           0, 0, 0,   0.0f, 0, false, -48.0f },
        { "Raga Bhairavi · C",        0, "Bhairavi (Hindustani)", 0, 0, 0,   0.0f, 0, false, -48.0f },
        { "Voice to Chords · C Major", 0, "Major",                0, 0, 0,  20.0f, 0, true,  -45.0f },
    };

    constexpr int kNumFactory = (int) (sizeof (kFactory) / sizeof (kFactory[0]));

    void addParam (juce::XmlElement& xml, const char* id, double value)
    {
        auto* p = xml.createNewChildElement ("PARAM");
        p->setAttribute ("id", id);
        p->setAttribute ("value", value);
    }
}

juce::File getUserFolder()
{
    return PluginCatalog::getUserDataFolder().getChildFile ("Presets");
}

int getNumFactoryPresets()                 { return kNumFactory; }

juce::String getFactoryName (int index)
{
    return juce::isPositiveAndBelow (index, kNumFactory) ? juce::String::fromUTF8 (kFactory[index].name) : juce::String();
}

std::unique_ptr<juce::XmlElement> createFactory (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumFactory))
        return nullptr;

    const auto& f = kFactory[index];
    const int scale = juce::jmax (0, ScaleChordEngine::getAllScaleNames().indexOf (juce::String::fromUTF8 (f.scale)));

    auto xml = std::make_unique<juce::XmlElement> (kRootTag);
    xml->setAttribute ("version", 1);
    xml->setAttribute ("name", juce::String::fromUTF8 (f.name));
    addParam (*xml, "rootKey",    f.root);
    addParam (*xml, "scale",      scale);
    addParam (*xml, "mode",       f.mode);
    addParam (*xml, "chordExt",   f.chordExt);
    addParam (*xml, "complexVar", f.complexVar);
    addParam (*xml, "strumMs",    f.strumMs);
    addParam (*xml, "strumDir",   f.strumDir);
    addParam (*xml, "micOn",      f.micOn ? 1.0 : 0.0);
    addParam (*xml, "micGate",    f.gateDb);
    return xml;
}

juce::Array<Item> list()
{
    juce::Array<Item> result;
    for (int i = 0; i < kNumFactory; ++i)
        result.add ({ getFactoryName (i), {}, i });

    juce::Array<Item> user;
    for (const auto& entry : juce::RangedDirectoryIterator (getUserFolder(), false, juce::String ("*") + kExtension,
                                                            juce::File::findFiles))
        user.add ({ entry.getFile().getFileNameWithoutExtension(), entry.getFile(), -1 });

    std::sort (user.begin(), user.end(), [] (const Item& a, const Item& b) { return a.name.compareNatural (b.name) < 0; });
    result.addArray (user);
    return result;
}

std::unique_ptr<juce::XmlElement> load (const Item& item)
{
    if (item.isFactory())
        return createFactory (item.factoryIndex);

    auto xml = juce::XmlDocument::parse (item.file);
    if (xml == nullptr || ! xml->hasTagName (kRootTag))
        return nullptr;
    return xml;
}

juce::String cleanName (const juce::String& name)
{
    auto s = name.trim().replaceCharacters ("/\\:*?\"<>|", "---------");
    while (s.startsWithChar ('.'))
        s = s.substring (1);
    return s.substring (0, 64).trim();
}

bool userPresetExists (const juce::String& name)
{
    const auto clean = cleanName (name);
    return clean.isNotEmpty() && getUserFolder().getChildFile (clean + kExtension).existsAsFile();
}

juce::Result save (const juce::String& name, const juce::XmlElement& preset)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty())
        return juce::Result::fail (juce::String::fromUTF8 ("Escribe un nombre"));

    const auto folder = getUserFolder();
    if (! folder.createDirectory())
        return juce::Result::fail (juce::String::fromUTF8 ("No se pudo crear la carpeta de presets"));

    auto copy = preset;
    copy.setAttribute ("name", clean);
    if (! copy.writeTo (folder.getChildFile (clean + kExtension)))
        return juce::Result::fail (juce::String::fromUTF8 ("No se pudo guardar el preset"));

    return juce::Result::ok();
}

bool remove (const Item& item)
{
    return ! item.isFactory() && item.file.existsAsFile() && item.file.deleteFile();
}

} // namespace smartkeys::presets
