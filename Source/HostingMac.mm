/*
  ==============================================================================

    HostingMac.mm
    SmartKeys (MIWN) — Piezas de macOS de la ranura de instrumento.

    1. Instrumentos Audio Unit registrados en macOS.

    Sólo se pregunta al registro de componentes (AudioComponentFindNext): no se
    abre ni se instancia ningún plugin, así que la lista sale al instante y un
    plugin defectuoso no puede cerrar el programa mientras se muestra el menú.
    Las descripciones usan el mismo identificador que JUCE
    ("AudioUnit:Synths/aumu,SUBT,MANU") para que la carga y los proyectos
    guardados encuentren el mismo plugin.

    2.0.3: las etiquetas de cada uno ("Drums", "Synthesizer"…), también del
    registro (AVAudioUnitComponentManager, sin abrirlos). Los de batería o
    percusión llevan la categoría "Synth|Drums" y el catálogo no los ofrece.

  ==============================================================================
*/

#include "PluginCatalog.h"

#include <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

namespace smartkeys
{

static juce::String osTypeToString (OSType type)
{
    const juce::juce_wchar s[4] = { (juce::juce_wchar) ((type >> 24) & 0xff),
                                    (juce::juce_wchar) ((type >> 16) & 0xff),
                                    (juce::juce_wchar) ((type >> 8) & 0xff),
                                    (juce::juce_wchar) (type & 0xff) };
    return juce::String (s, 4);
}

// Los instrumentos que el sistema etiqueta como batería o percusión ("SUBT,MANU" de cada uno).
// macOS da las etiquetas estándar traducidas ("Batería") y las del usuario.
static juce::StringArray drumTaggedInstruments()
{
    juce::StringArray result;

    @autoreleasepool
    {
        AudioComponentDescription wanted {};
        wanted.componentType = kAudioUnitType_MusicDevice;

        NSArray<AVAudioUnitComponent*>* components = [[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
                                                          componentsMatchingDescription: wanted];
        for (AVAudioUnitComponent* component in components)
        {
            bool drums = false;
            for (NSString* tag in [component allTagNames])
                drums = drums || PluginCatalog::isDrumTag (juce::String::fromUTF8 ([tag UTF8String]));

            if (drums)
            {
                const auto desc = [component audioComponentDescription];
                result.add (osTypeToString (desc.componentSubType) + "," + osTypeToString (desc.componentManufacturer));
            }
        }
    }

    return result;
}

juce::Array<juce::PluginDescription> listAudioUnitInstruments()
{
    juce::Array<juce::PluginDescription> result;
    const auto drumTagged = drumTaggedInstruments();

    AudioComponentDescription wanted {};
    wanted.componentType = kAudioUnitType_MusicDevice;

    AudioComponent component = nullptr;
    while ((component = AudioComponentFindNext (component, &wanted)) != nullptr)
    {
        AudioComponentDescription desc {};
        if (AudioComponentGetDescription (component, &desc) != noErr || desc.componentType != kAudioUnitType_MusicDevice)
            continue;

        // "Fabricante: Nombre"
        juce::String name, manufacturer;
        CFStringRef cfName = nullptr;
        if (AudioComponentCopyName (component, &cfName) == noErr && cfName != nullptr)
        {
            name = juce::String::fromCFString (cfName);
            CFRelease (cfName);
        }
        if (name.containsChar (':'))
        {
            manufacturer = name.upToFirstOccurrenceOf (":", false, false).trim();
            name = name.fromFirstOccurrenceOf (":", false, false).trim();
        }
        if (name.isEmpty())
            name = osTypeToString (desc.componentSubType);
        if (manufacturer.isEmpty())
            manufacturer = osTypeToString (desc.componentManufacturer);

        juce::String version;
        UInt32 versionNumber = 0;
        if (AudioComponentGetVersion (component, &versionNumber) == noErr)
            version << (int) (versionNumber >> 16) << "." << (int) ((versionNumber >> 8) & 0xff) << "." << (int) (versionNumber & 0xff);

        juce::PluginDescription d;
        d.name = name;
        d.descriptiveName = name;
        d.manufacturerName = manufacturer;
        d.version = version;
        d.pluginFormatName = "AudioUnit";
        d.category = drumTagged.contains (osTypeToString (desc.componentSubType) + "," + osTypeToString (desc.componentManufacturer))
                         ? "Synth|Drums" : "Synth";
        d.isInstrument = true;
        d.fileOrIdentifier = "AudioUnit:Synths/" + osTypeToString (desc.componentType) + ","
                           + osTypeToString (desc.componentSubType) + ","
                           + osTypeToString (desc.componentManufacturer);
        d.uniqueId = d.deprecatedUid = ((int) desc.componentType) ^ ((int) desc.componentSubType) ^ ((int) desc.componentManufacturer);
        d.numInputChannels = 0;
        d.numOutputChannels = 2;
        result.add (d);
    }

    return result;
}

} // namespace smartkeys
