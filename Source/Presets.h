/*
  ==============================================================================

    Presets.h
    SmartKeys (MIWN) — Presets propios de SmartKeys.

    Un preset guarda los ajustes de SmartKeys (KEY, SCALE y las notas
    personalizadas, modo, voicing, estilo COMPLEX, STRUM SPEED + ASC/DESC, MIC
    VOICE y su GATE) y, si se quiere, también el instrumento cargado con su
    sonido (una "cadena" favorita: SmartKeys + Serum con su patch, por ejemplo).
    El volumen (GAIN) no forma parte del preset: es de la mezcla.

    Los presets del usuario son archivos XML (.skpreset) en
      macOS:   ~/Library/Application Support/MIWN/SmartKeys/Presets
      Windows: %APPDATA%\MIWN\SmartKeys\Presets
    Los de fábrica van dentro del plugin.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace smartkeys::presets
{

inline constexpr const char* kRootTag = "SmartKeysPreset";
inline constexpr const char* kExtension = ".skpreset";

struct Item
{
    juce::String name;
    juce::File file;           // vacío en los de fábrica
    int factoryIndex = -1;     // >= 0 en los de fábrica

    bool isFactory() const noexcept   { return factoryIndex >= 0; }
};

juce::File getUserFolder();

// De fábrica primero; después los del usuario por orden alfabético
juce::Array<Item> list();

std::unique_ptr<juce::XmlElement> load (const Item&);

// Nombre válido para un archivo ("Neo Soul / Keys" -> "Neo Soul - Keys")
juce::String cleanName (const juce::String&);
bool userPresetExists (const juce::String& name);
juce::Result save (const juce::String& name, const juce::XmlElement& preset);
bool remove (const Item&);

// Preset de fábrica 'index' (parámetros por nombre de escala, para no depender del orden)
int getNumFactoryPresets();
std::unique_ptr<juce::XmlElement> createFactory (int index);
juce::String getFactoryName (int index);

} // namespace smartkeys::presets
