/*
  ==============================================================================

    ScaleChordEngine.h
    SmartKeys — Motor matemático de escalas, cuantización y acordes diatónicos.

    - Base de datos completa de 43 escalas (básicas, occidentales, maqam, raga).
    - Cada grado de escala se guarda como {semitono 12-TET, desviación en cents}.
      El semitono decide la cuantización MIDI (el MIDI es 12-TET); la desviación
      en cents se usa opcionalmente para afinar microtonalmente el piano interno
      (cuartos de tono en maqam, entonación justa en Justonic / Carnatic, etc.).
    - Todo el trabajo del hilo de audio usa arrays de tamaño fijo: cero
      asignaciones de memoria tras la construcción.

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace smartkeys
{

//==============================================================================
enum class ScaleCategory : int
{
    Basic = 0,
    Western,
    Arabic,
    Indian
};

struct ScaleDegree
{
    int   semitone;   // 0..11 relativo a la tónica (posición MIDI 12-TET)
    float cents;      // desviación microtonal respecto al semitono 12-TET
};

struct ScaleDefinition
{
    const char*    name;
    ScaleCategory  category;
    int            numDegrees;
    ScaleDegree    degrees[12];
};

//==============================================================================
// Desviaciones de entonación justa usadas en Justonic y en las escalas Carnatic.
namespace ji
{
    // Ratios: 1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 16/9, 15/8
    constexpr float swara[12] = { 0.0f, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f,
                                  -9.78f, 1.96f, 13.69f, -15.64f, -3.91f, -11.73f };
}

//==============================================================================
// BASE DE DATOS COMPLETA DE ESCALAS (orden = índice del parámetro "scale")
//==============================================================================
static constexpr int kNumScales = 43;

static constexpr ScaleDefinition kScaleDatabase[kNumScales] =
{
    //------------------------------------------------------------------ Básicas
    { "Major",     ScaleCategory::Basic, 7, { {0,0},{2,0},{4,0},{5,0},{7,0},{9,0},{11,0} } },
    { "Minor",     ScaleCategory::Basic, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    { "Chromatic", ScaleCategory::Basic, 12, { {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},{8,0},{9,0},{10,0},{11,0} } },

    //------------------------------------------------------------- Occidentales
    { "Harmonic Minor",          ScaleCategory::Western, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{8,0},{11,0} } },
    { "Melodic Minor (Asc)",     ScaleCategory::Western, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{9,0},{11,0} } },
    // Ascendente (6ª y 7ª mayores) + descendente (6ª y 7ª menores) combinadas
    { "Melodic Minor (Asc+Dec)", ScaleCategory::Western, 9, { {0,0},{2,0},{3,0},{5,0},{7,0},{8,0},{9,0},{10,0},{11,0} } },
    // Entonación justa: 1, 9/8, 5/4, 4/3, 3/2, 5/3, 15/8
    { "Major Justonic",          ScaleCategory::Western, 7, { {0,0.0f},{2,3.91f},{4,-13.69f},{5,-1.96f},{7,1.96f},{9,-15.64f},{11,-11.73f} } },
    // Entonación justa: 1, 9/8, 6/5, 4/3, 3/2, 8/5, 9/5
    { "Minor Justonic",          ScaleCategory::Western, 7, { {0,0.0f},{2,3.91f},{3,15.64f},{5,-1.96f},{7,1.96f},{8,13.69f},{10,17.60f} } },
    { "Dorian",                  ScaleCategory::Western, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{9,0},{10,0} } },
    { "Mixolydian",              ScaleCategory::Western, 7, { {0,0},{2,0},{4,0},{5,0},{7,0},{9,0},{10,0} } },
    // Blues mayor (1 2 b3 3 5 6)
    { "Blues",                   ScaleCategory::Western, 6, { {0,0},{2,0},{3,0},{4,0},{7,0},{9,0} } },
    { "Phrygian",                ScaleCategory::Western, 7, { {0,0},{1,0},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    { "Locrian",                 ScaleCategory::Western, 7, { {0,0},{1,0},{3,0},{5,0},{6,0},{8,0},{10,0} } },
    { "Lydian",                  ScaleCategory::Western, 7, { {0,0},{2,0},{4,0},{6,0},{7,0},{9,0},{11,0} } },
    { "Whole Tone",              ScaleCategory::Western, 6, { {0,0},{2,0},{4,0},{6,0},{8,0},{10,0} } },
    { "Diminished (W-H)",        ScaleCategory::Western, 8, { {0,0},{2,0},{3,0},{5,0},{6,0},{8,0},{9,0},{11,0} } },
    { "Diminished (H-W)",        ScaleCategory::Western, 8, { {0,0},{1,0},{3,0},{4,0},{6,0},{7,0},{9,0},{10,0} } },
    // Bebop dominante (mixolidio + 7ª mayor de paso)
    { "Bebop",                   ScaleCategory::Western, 8, { {0,0},{2,0},{4,0},{5,0},{7,0},{9,0},{10,0},{11,0} } },
    { "Major Pentatonic",        ScaleCategory::Western, 5, { {0,0},{2,0},{4,0},{7,0},{9,0} } },
    { "Minor Pentatonic",        ScaleCategory::Western, 5, { {0,0},{3,0},{5,0},{7,0},{10,0} } },
    { "Major Pentatonic flat 3rd", ScaleCategory::Western, 5, { {0,0},{2,0},{3,0},{7,0},{9,0} } },
    // Pentatónica menor + blue note (b5)
    { "Minor Pentatonic + Blues", ScaleCategory::Western, 6, { {0,0},{3,0},{5,0},{6,0},{7,0},{10,0} } },
    // Bebop dominante + blue notes (b3, b5)
    { "Bebop + Blues",           ScaleCategory::Western, 10, { {0,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},{9,0},{10,0},{11,0} } },
    // Mayor diatónica en afinación pitagórica (quintas puras 3/2)
    { "Pythagorean",             ScaleCategory::Western, 7, { {0,0.0f},{2,3.91f},{4,7.82f},{5,-1.96f},{7,1.96f},{9,5.87f},{11,9.78f} } },

    //------------------------------------------------------------ Árabes (maqam)
    // Cuartos de tono: semitono de la nota natural + desviación de -50 cents.
    { "Maqam Rast (Rast-Rast)", ScaleCategory::Arabic, 7, { {0,0},{2,0},{4,-50.0f},{5,0},{7,0},{9,0},{11,-50.0f} } },
    { "Maqam Saba",             ScaleCategory::Arabic, 7, { {0,0},{2,-50.0f},{3,0},{4,0},{7,0},{8,0},{10,0} } },
    { "Maqam Bayati",           ScaleCategory::Arabic, 7, { {0,0},{2,-50.0f},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    { "Maqam Nahawand",         ScaleCategory::Arabic, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{8,0},{11,0} } },
    { "Maqam Hijaz",            ScaleCategory::Arabic, 7, { {0,0},{1,0},{4,0},{5,0},{7,0},{8,0},{10,0} } },
    { "Maqam Kurd",             ScaleCategory::Arabic, 7, { {0,0},{1,0},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    // Huzam: la tónica es la nota medio-bemol (Sikah); resto relativo a ella.
    { "Maqam Huzam",            ScaleCategory::Arabic, 7, { {0,-50.0f},{1,0},{3,0},{4,0},{7,0},{8,0},{10,0} } },

    //------------------------------------------------------------- Indias (raga)
    // Hindustani: thaat en 12-TET.  Carnatic: raga equivalente con afinación de shrutis (JI).
    { "Bilawal (Hindustani)",  ScaleCategory::Indian, 7, { {0,0},{2,0},{4,0},{5,0},{7,0},{9,0},{11,0} } },
    { "Bilawal (Carnatic)",    ScaleCategory::Indian, 7, { {0,ji::swara[0]},{2,ji::swara[2]},{4,ji::swara[4]},{5,ji::swara[5]},{7,ji::swara[7]},{9,ji::swara[9]},{11,ji::swara[11]} } },
    { "Kafi (Hindustani)",     ScaleCategory::Indian, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{9,0},{10,0} } },
    { "Kafi (Carnatic)",       ScaleCategory::Indian, 7, { {0,ji::swara[0]},{2,ji::swara[2]},{3,ji::swara[3]},{5,ji::swara[5]},{7,ji::swara[7]},{9,ji::swara[9]},{10,ji::swara[10]} } },
    { "Bhairavi (Hindustani)", ScaleCategory::Indian, 7, { {0,0},{1,0},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    // Bhairavi carnático: S R2 G2 M1 P D1 D2 N2 (ambos dhaivats)
    { "Bhairavi (Carnatic)",   ScaleCategory::Indian, 8, { {0,ji::swara[0]},{2,ji::swara[2]},{3,ji::swara[3]},{5,ji::swara[5]},{7,ji::swara[7]},{8,ji::swara[8]},{9,ji::swara[9]},{10,ji::swara[10]} } },
    { "Kalyan (Hindustani)",   ScaleCategory::Indian, 7, { {0,0},{2,0},{4,0},{6,0},{7,0},{9,0},{11,0} } },
    { "Kalyan (Carnatic)",     ScaleCategory::Indian, 7, { {0,ji::swara[0]},{2,ji::swara[2]},{4,ji::swara[4]},{6,ji::swara[6]},{7,ji::swara[7]},{9,ji::swara[9]},{11,ji::swara[11]} } },
    { "Khamaj (Hindustani)",   ScaleCategory::Indian, 7, { {0,0},{2,0},{4,0},{5,0},{7,0},{9,0},{10,0} } },
    { "Khamaj (Carnatic)",     ScaleCategory::Indian, 7, { {0,ji::swara[0]},{2,ji::swara[2]},{4,ji::swara[4]},{5,ji::swara[5]},{7,ji::swara[7]},{9,ji::swara[9]},{10,ji::swara[10]} } },
    { "Asavari (Hindustani)",  ScaleCategory::Indian, 7, { {0,0},{2,0},{3,0},{5,0},{7,0},{8,0},{10,0} } },
    // Asaveri carnático (janya de Hanumatodi): S R1 G2 M1 P D1 N2
    { "Asavari (Carnatic)",    ScaleCategory::Indian, 7, { {0,ji::swara[0]},{1,ji::swara[1]},{3,ji::swara[3]},{5,ji::swara[5]},{7,ji::swara[7]},{8,ji::swara[8]},{10,ji::swara[10]} } },
};

//==============================================================================
enum class OperationMode : int
{
    SmartChord   = 0,
    SingleNote   = 1,
    ComplexChord = 2
};

enum class ChordExtension : int
{
    Triad = 0,        // k, k+2, k+4
    TriadOctave,      // k, k+2, k+4, k+7
    Seventh           // k, k+2, k+4, k+6
};

// Variaciones del modo COMPLEX CHORDS (mano izquierda común: d0-12 y grado(k+4)-12)
enum class ComplexVariation : int
{
    LushSevenNine = 0,   // "7th & 9th Lush"        : k, k+2, k+6, k+8, k+11
    NeoSoulNineEleven,   // "9th & 11th Neo-Soul"   : k, k+2, k+6, k+8, k+10
    CinematicAdd9        // "Cinematic Spread Add9" : k, k+8, k+9, k+11, k+14
};

//==============================================================================
class ScaleChordEngine
{
public:
    static constexpr int kMaxChordNotes = 8;

    // Balance dinámico de velocidad del modo COMPLEX CHORDS (velocity voicing)
    static constexpr float kBassVelocityScale   = 0.95f;   // mano izquierda: peso
    static constexpr float kInnerVelocityScale  = 0.80f;   // voces centrales: calidez
    static constexpr float kTopVelocityScale    = 0.90f;   // voz más aguda: melodía superior

    // Notas de salida de una tecla + factor de velocidad por voz (sin asignaciones)
    struct Voicing
    {
        std::array<int,   kMaxChordNotes> notes {};
        std::array<float, kMaxChordNotes> velocityScale {};
        int count = 0;
    };

    static juce::StringArray getComplexVariationNames()
    {
        return { "7th & 9th Lush", "9th & 11th Neo-Soul", "Cinematic Spread Add9" };
    }

    ScaleChordEngine()
    {
        setScale (0);
    }

    //==========================================================================
    // Base de datos
    static int getNumScales() noexcept                       { return kNumScales; }
    static const ScaleDefinition& getScale (int index) noexcept
    {
        return kScaleDatabase[juce::jlimit (0, kNumScales - 1, index)];
    }

    static juce::StringArray getAllScaleNames()
    {
        juce::StringArray names;
        for (const auto& s : kScaleDatabase)
            names.add (juce::String::fromUTF8 (s.name));
        return names;
    }

    // Máscara de 12 bits (bit 0 = tónica) de los semitonos de una escala.
    static uint16_t getScaleIntervalMask (int scaleIndex) noexcept
    {
        const auto& s = getScale (scaleIndex);
        uint16_t mask = 0;
        for (int i = 0; i < s.numDegrees; ++i)
            mask = (uint16_t) (mask | (1u << (unsigned) s.degrees[i].semitone));
        return mask;
    }

    // Máscara de 12 bits de clases de altura absolutas (bit 0 = C) para tónica + escala.
    static uint16_t getPitchClassMask (int rootKey, int scaleIndex) noexcept
    {
        const uint16_t rel = getScaleIntervalMask (scaleIndex);
        uint16_t abs = 0;
        for (int i = 0; i < 12; ++i)
            if ((rel >> i) & 1u)
                abs = (uint16_t) (abs | (1u << (unsigned) ((rootKey + i) % 12)));
        return abs;
    }

    //==========================================================================
    // Configuración (hilo de audio o de mensajes; sin asignaciones)
    void setRootKey (int newRoot) noexcept
    {
        newRoot = ((newRoot % 12) + 12) % 12;
        if (newRoot != rootKey)
        {
            rootKey = newRoot;
            applyScaleToCustomNotes();
        }
    }

    void setScale (int newScaleIndex) noexcept
    {
        scaleIndex = juce::jlimit (0, kNumScales - 1, newScaleIndex);
        applyScaleToCustomNotes();
    }

    // Establece tónica + escala + máscara personalizada de una sola vez
    // (no sobrescribe la personalización manual del usuario).
    void setState (int newRoot, int newScaleIndex, uint16_t pitchClassMask) noexcept
    {
        rootKey    = ((newRoot % 12) + 12) % 12;
        scaleIndex = juce::jlimit (0, kNumScales - 1, newScaleIndex);
        setCustomMask (pitchClassMask);
    }

    void setReferencePitch (float hz) noexcept   { referencePitchHz = juce::jlimit (400.0f, 480.0f, hz); }
    float getReferencePitch() const noexcept     { return referencePitchHz; }

    void setMode (OperationMode m) noexcept       { mode = m; }
    OperationMode getMode() const noexcept        { return mode; }

    void setChordExtension (ChordExtension e) noexcept { extension = e; }
    ChordExtension getChordExtension() const noexcept  { return extension; }

    void setComplexVariation (ComplexVariation v) noexcept { complexVariation = v; }
    ComplexVariation getComplexVariation() const noexcept  { return complexVariation; }

    int getRootKey() const noexcept     { return rootKey; }
    int getScaleIndex() const noexcept  { return scaleIndex; }

    //==========================================================================
    // Personalización manual de las 12 notas (índice = clase de altura absoluta, 0 = C)
    void setNoteActive (int pitchClass, bool active) noexcept
    {
        pitchClass = ((pitchClass % 12) + 12) % 12;
        customActiveNotes[pitchClass] = active;
        rebuildTables();
    }

    void toggleNote (int pitchClass) noexcept
    {
        pitchClass = ((pitchClass % 12) + 12) % 12;
        setNoteActive (pitchClass, ! customActiveNotes[pitchClass]);
    }

    bool isNoteActive (int pitchClass) const noexcept
    {
        return customActiveNotes[((pitchClass % 12) + 12) % 12];
    }

    void setCustomMask (uint16_t mask) noexcept
    {
        for (int i = 0; i < 12; ++i)
            customActiveNotes[i] = ((mask >> i) & 1u) != 0;
        rebuildTables();
    }

    uint16_t getCustomMask() const noexcept
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (customActiveNotes[i])
                m = (uint16_t) (m | (1u << (unsigned) i));
        return m;
    }

    bool isCustomised() const noexcept
    {
        return getCustomMask() != getPitchClassMask (rootKey, scaleIndex);
    }

    //==========================================================================
    // Afinación
    float midiNoteToFrequency (float midiNote) const noexcept
    {
        return referencePitchHz * std::pow (2.0f, (midiNote - 69.0f) / 12.0f);
    }

    float frequencyToMidiNote (float hz) const noexcept
    {
        return 69.0f + 12.0f * std::log2 (juce::jmax (1.0e-6f, hz) / referencePitchHz);
    }

    // Desviación microtonal (cents) de una clase de altura según la escala activa
    float getCentsDeviation (int pitchClass) const noexcept
    {
        return centsByPitchClass[(size_t) (((pitchClass % 12) + 12) % 12)];
    }

    // Frecuencia con afinación microtonal opcional de la escala
    float getTunedFrequency (int midiNote, bool useScaleTuning) const noexcept
    {
        const float cents = useScaleTuning ? getCentsDeviation (midiNote % 12) : 0.0f;
        return midiNoteToFrequency ((float) midiNote + cents * 0.01f);
    }

    //==========================================================================
    // Lista global ordenada de notas válidas (0..127)
    int getNumScaleNotes() const noexcept           { return numScaleNotes; }
    int getScaleNote (int index) const noexcept     { return scaleNotes[(size_t) juce::jlimit (0, juce::jmax (0, numScaleNotes - 1), index)]; }
    int getIndexInScale (int midiNote) const noexcept
    {
        return (midiNote >= 0 && midiNote < 128) ? indexInScale[(size_t) midiNote] : -1;
    }
    int getNumActivePitchClasses() const noexcept   { return numActivePitchClasses; }

    //==========================================================================
    // MODO SINGLE NOTE: cuantización a la nota válida más cercana
    // (empate -> se elige la nota inferior).
    int quantizeNote (int midiNoteIn) const noexcept
    {
        return quantizedTable[(size_t) juce::jlimit (0, 127, midiNoteIn)];
    }

    // Cuantización de altura flotante (detector de voz): nota de escala más cercana.
    int quantizeFloatNote (float midiNote) const noexcept
    {
        const int base = juce::jlimit (0, 127, (int) std::floor (midiNote));
        int best = quantizedTable[(size_t) base];
        float bestDist = std::abs (midiNote - (float) best);

        for (int candidateSeed : { base + 1, base - 1, base + 2 })
        {
            if (candidateSeed < 0 || candidateSeed > 127)
                continue;
            const int c = quantizedTable[(size_t) candidateSeed];
            const float d = std::abs (midiNote - (float) c);
            if (d < bestDist)
            {
                best = c;
                bestDist = d;
            }
        }
        return best;
    }

    // Nota vecina de escala (dirección +1 / -1) o -1 si no existe
    int getNeighbourScaleNote (int scaleNote, int direction) const noexcept
    {
        const int idx = getIndexInScale (scaleNote);
        if (idx < 0)
            return -1;
        const int n = idx + (direction > 0 ? 1 : -1);
        return (n >= 0 && n < numScaleNotes) ? scaleNotes[(size_t) n] : -1;
    }

    //==========================================================================
    // MODO SMART CHORD
    //   1. Cuantiza N_in -> d0 (fundamental).
    //   2. Localiza k = índice de d0 en la lista global de la escala.
    //   3. Escalas heptatónicas: superpone grados k, k+2, k+4 (+k+6 ó +k+7).
    //      Otras escalas (pentatónicas, cromática, bebop, disminuidas...):
    //      búsqueda terciana dentro de la escala (3ª, 5ª, 7ª) para que el
    //      resultado siga siendo un acorde y no un clúster.
    // Devuelve el número de notas escritas en 'out'.
    int buildChord (int midiNoteIn, std::array<int, kMaxChordNotes>& out) const noexcept
    {
        int count = 0;
        const int d0 = quantizeNote (midiNoteIn);
        const int k = getIndexInScale (d0);

        if (k < 0)
        {
            out[0] = d0;
            return 1;
        }

        auto pushNote = [&] (int note)
        {
            if (note < 0 || note > 127 || count >= kMaxChordNotes)
                return;
            for (int i = 0; i < count; ++i)
                if (out[(size_t) i] == note)
                    return;
            out[(size_t) count++] = note;
        };

        if (numActivePitchClasses == 7)
        {
            auto degree = [&] (int offset) -> int
            {
                const int idx = k + offset;
                return (idx >= 0 && idx < numScaleNotes) ? scaleNotes[(size_t) idx] : -1;
            };

            pushNote (d0);
            pushNote (degree (2));
            pushNote (degree (4));

            if (extension == ChordExtension::Seventh)          pushNote (degree (6));
            else if (extension == ChordExtension::TriadOctave) pushNote (degree (7));
        }
        else
        {
            pushNote (d0);

            if (numActivePitchClasses == 1)
            {
                pushNote (d0 + 12);
                if (extension != ChordExtension::Triad)
                    pushNote (d0 + 24);
            }
            else
            {
                static constexpr int thirdPrefs[]   = { 4, 3, 5, 2 };   // 3M, 3m, sus4, sus2
                static constexpr int fifthPrefs[]   = { 7, 6, 8 };      // 5J, 5dim, 5aug
                static constexpr int seventhPrefs[] = { 10, 11, 9 };    // 7m, 7M, 6

                const int third = findIntervalInScale (d0, thirdPrefs, 4);
                pushNote (third);

                int fifth = findIntervalInScale (d0, fifthPrefs, 3);
                if (fifth < 0 && third >= 0)
                    fifth = getNeighbourScaleNote (third, +1);
                pushNote (fifth);

                if (extension == ChordExtension::Seventh)
                    pushNote (findIntervalInScale (d0, seventhPrefs, 3));
                else if (extension == ChordExtension::TriadOctave)
                    pushNote (d0 + 12);
            }
        }

        // Ordenación por inserción (máx. 8 elementos, sin asignaciones)
        for (int i = 1; i < count; ++i)
        {
            const int key = out[(size_t) i];
            int j = i - 1;
            while (j >= 0 && out[(size_t) j] > key)
            {
                out[(size_t) (j + 1)] = out[(size_t) j];
                --j;
            }
            out[(size_t) (j + 1)] = key;
        }
        return count;
    }

    //==========================================================================
    // MODO COMPLEX CHORDS — voicing de dos manos de pianista (5 a 7 notas)
    //
    //   Mano izquierda (cuerpo y peso):  d0 - 12  y  grado(k+4) - 12
    //   Mano derecha (open voicing), según la variación:
    //     7th & 9th Lush        : k, k+2, k+6, k+8, k+11 (5ª arriba como voz superior)
    //     9th & 11th Neo-Soul   : k, k+2, k+6, k+8, k+10
    //     Cinematic Spread Add9 : k, k+8, k+9 (10ª), k+11 (12ª), k+14 (2 octavas)
    //
    //   Regla anti-choques: una extensión (9ª / 11ª) que quede a 1 ó 13 semitonos
    //   (2ª menor / 9ª menor) de otra voz se sustituye por el siguiente candidato
    //   diatónico: 11ª sobre 3ª mayor -> 13ª (F Maj13, C13); b9 -> 11ª u octava.
    //   Así cada grado suena "limpio" sin salir nunca de la escala.
    //
    //   Escalas no heptatónicas: cada grado se traduce a su función (3ª, 5ª, 7ª,
    //   9ª...) y se busca el intervalo más cercano disponible dentro de la escala.
    //
    //   Guarda de registro: si el voicing se sale de 0..127 se transpone entero
    //   por octavas, así nunca se pierden voces en los extremos del teclado.
    //
    //   Velocity voicing: mano izquierda 95 %, voces centrales 80 %, voz más aguda 90 %.
    int buildComplexChord (int midiNoteIn, Voicing& out) const noexcept
    {
        out.count = 0;
        const int d0 = quantizeNote (midiNoteIn);
        const bool heptatonic = (numActivePitchClasses == 7);

        int numSlots = 0;
        const VoiceSlot* slots = getVariationSlots (complexVariation, numSlots);

        std::array<int, kMaxChordNotes> notes {};
        std::array<bool, kMaxChordNotes> isBass {};
        int count = 0;

        auto resolve = [&] (int degreeOffset, int octaveShift) -> int
        {
            if (heptatonic)
                return stepScale (d0, degreeOffset) + 12 * octaveShift;

            const int function = degreeOffset % 7;
            const int octaves  = degreeOffset / 7 + octaveShift;
            const auto& prefs = kFunctionPrefs[function];
            for (int i = 0; i < prefs.count; ++i)
                if (isPitchClassActive (d0 + prefs.intervals[i]))
                    return d0 + prefs.intervals[i] + 12 * octaves;
            return kNoNote;
        };

        auto contains = [&] (int note) -> bool
        {
            for (int i = 0; i < count; ++i)
                if (notes[(size_t) i] == note)
                    return true;
            return false;
        };

        auto clashes = [&] (int note) -> bool
        {
            for (int i = 0; i < count; ++i)
            {
                const int d = std::abs (note - notes[(size_t) i]);
                if (d == 0 || d == 1 || d == 13)
                    return true;
            }
            return false;
        };

        auto push = [&] (int note, bool bass)
        {
            if (count >= kMaxChordNotes)
                return;
            notes[(size_t) count] = note;
            isBass[(size_t) count] = bass;
            ++count;
        };

        // 1) Tonos del acorde y duplicaciones (siempre presentes si existen en la escala)
        for (int s = 0; s < numSlots; ++s)
        {
            const auto& slot = slots[s];
            if (slot.isExtension)
                continue;
            const int n = resolve (slot.candidates[0], slot.octaveShift);
            if (n != kNoNote && ! contains (n))
                push (n, slot.isBass);
        }

        // 2) Extensiones con sustitución anti-choques
        for (int s = 0; s < numSlots; ++s)
        {
            const auto& slot = slots[s];
            if (! slot.isExtension)
                continue;
            for (int c = 0; c < slot.numCandidates; ++c)
            {
                const int n = resolve (slot.candidates[c], slot.octaveShift);
                if (n != kNoNote && ! clashes (n))
                {
                    push (n, false);
                    break;
                }
            }
        }

        // 3) Relleno mínimo de 5 notas (escalas personalizadas muy dispersas)
        static constexpr int fillers[][2] = { { 0, 1 }, { 4, 1 }, { 2, 1 }, { 0, 2 }, { 4, 2 }, { 2, 2 }, { 0, 3 } };
        for (int pass = 0; pass < 2 && count < 5; ++pass)
        {
            for (const auto& f : fillers)
            {
                if (count >= 5)
                    break;
                const int n = resolve (f[0], f[1]);
                if (n == kNoNote || contains (n) || (pass == 0 && clashes (n)))
                    continue;
                push (n, false);
            }
        }

        if (count == 0)
            push (d0, false);

        // 4) Guarda de registro: transponer por octavas para quedar en 0..127
        int lo = notes[0], hi = notes[0];
        for (int i = 1; i < count; ++i)
        {
            lo = juce::jmin (lo, notes[(size_t) i]);
            hi = juce::jmax (hi, notes[(size_t) i]);
        }
        int shift = 0;
        while (hi + shift > 127 && lo + shift - 12 >= 0) shift -= 12;
        while (lo + shift < 0 && hi + shift + 12 <= 127) shift += 12;

        // 5) Ordenar (inserción) conservando el rol de cada voz
        for (int i = 0; i < count; ++i)
            notes[(size_t) i] += shift;
        for (int i = 1; i < count; ++i)
        {
            const int key = notes[(size_t) i];
            const bool keyBass = isBass[(size_t) i];
            int j = i - 1;
            while (j >= 0 && notes[(size_t) j] > key)
            {
                notes[(size_t) (j + 1)] = notes[(size_t) j];
                isBass[(size_t) (j + 1)] = isBass[(size_t) j];
                --j;
            }
            notes[(size_t) (j + 1)] = key;
            isBass[(size_t) (j + 1)] = keyBass;
        }

        // 6) Velocity voicing
        int topIndex = -1;
        for (int i = count - 1; i >= 0; --i)
            if (! isBass[(size_t) i]) { topIndex = i; break; }

        for (int i = 0; i < count; ++i)
        {
            const int n = notes[(size_t) i];
            if (n < 0 || n > 127)
                continue;   // imposible tras la guarda (el voicing abarca < 4 octavas)
            out.notes[(size_t) out.count] = n;
            out.velocityScale[(size_t) out.count] = isBass[(size_t) i] ? kBassVelocityScale
                                                   : (i == topIndex ? kTopVelocityScale : kInnerVelocityScale);
            ++out.count;
        }
        return out.count;
    }

    //==========================================================================
    // Despacho por modo: notas de salida + factor de velocidad de cada voz
    void generateVoicing (int midiNoteIn, Voicing& out) const noexcept
    {
        out.count = 0;

        if (mode == OperationMode::SingleNote)
        {
            out.notes[0] = quantizeNote (midiNoteIn);
            out.velocityScale[0] = 1.0f;
            out.count = 1;
            return;
        }

        if (mode == OperationMode::ComplexChord)
        {
            buildComplexChord (midiNoteIn, out);
            return;
        }

        std::array<int, kMaxChordNotes> chord {};
        const int n = buildChord (midiNoteIn, chord);
        for (int i = 0; i < n; ++i)
        {
            out.notes[(size_t) i] = chord[(size_t) i];
            out.velocityScale[(size_t) i] = 1.0f;
        }
        out.count = n;
    }

    //==========================================================================
    // Paso 3 del router: vector de notas de salida para una nota de entrada.
    // 'outputNotes' debe tener capacidad reservada (>= kMaxChordNotes) para que
    // push_back no asigne memoria en el hilo de audio.
    void generateOutputNotes (int midiNoteIn, std::vector<int>& outputNotes) const noexcept
    {
        Voicing v;
        generateVoicing (midiNoteIn, v);
        outputNotes.clear();
        for (int i = 0; i < v.count; ++i)
            outputNotes.push_back (v.notes[(size_t) i]);
    }

    // Variante con el factor de velocidad de cada nota (velocity voicing)
    void generateOutputNotes (int midiNoteIn, std::vector<int>& outputNotes,
                              std::vector<float>& velocityScales) const noexcept
    {
        Voicing v;
        generateVoicing (midiNoteIn, v);
        outputNotes.clear();
        velocityScales.clear();
        for (int i = 0; i < v.count; ++i)
        {
            outputNotes.push_back (v.notes[(size_t) i]);
            velocityScales.push_back (v.velocityScale[(size_t) i]);
        }
    }

    //==========================================================================
    // Nombres
    bool prefersFlats() const noexcept
    {
        // Tonalidades con bemoles: F, Bb, Eb, Ab, Db
        return rootKey == 5 || rootKey == 10 || rootKey == 3 || rootKey == 8 || rootKey == 1;
    }

    static juce::String getPitchClassName (int pitchClass, bool useFlats)
    {
        static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flats[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const int pc = ((pitchClass % 12) + 12) % 12;
        return useFlats ? flats[pc] : sharps[pc];
    }

    // Nombre con octava (C4 = MIDI 60)
    juce::String getNoteName (int midiNote) const
    {
        return getPitchClassName (midiNote, prefersFlats()) + juce::String ((midiNote / 12) - 1);
    }

    juce::String getScaleDisplayName() const
    {
        return getPitchClassName (rootKey, prefersFlats()) + " " + juce::String::fromUTF8 (getScale (scaleIndex).name);
    }

    // Identidad de un acorde: fundamental, bajo y sufijo funcional
    struct ChordIdentity
    {
        bool valid = false;        // false: no hay lectura coherente (se lista por notas)
        int rootPc = 0;
        int bassPc = 0;
        juce::String suffix;       // "m7", "Maj9", "dim", "sus4"...
        bool spaced = false;       // "F Maj", "B dim": sufijo separado por un espacio
        int thirdInterval = -1, fifthInterval = -1, seventhInterval = -1;
        int distinctPitchClasses = 0;
    };

    // Analiza funciones (3ª, 5ª, 7ª y tensiones 9/11/13) para cada fundamental
    // posible y elige la lectura más coherente (con preferencia por el bajo).
    static ChordIdentity identifyChord (const std::vector<int>& chordNotes)
    {
        ChordIdentity id;
        if (chordNotes.empty())
            return id;

        const int bass = *std::min_element (chordNotes.begin(), chordNotes.end());
        uint16_t pcMask = 0;
        for (int n : chordNotes)
            pcMask = (uint16_t) (pcMask | (1u << (unsigned) (((n % 12) + 12) % 12)));

        for (int i = 0; i < 12; ++i)
            id.distinctPitchClasses += (pcMask >> i) & 1;

        const int bassPc = ((bass % 12) + 12) % 12;
        id.bassPc = bassPc;
        id.rootPc = bassPc;

        if (id.distinctPitchClasses == 1)
            return id;

        auto rotate = [] (uint16_t mask, int root) -> uint16_t
        {
            uint16_t r = 0;
            for (int i = 0; i < 12; ++i)
                if ((mask >> ((i + root) % 12)) & 1u)
                    r = (uint16_t) (r | (1u << (unsigned) i));
            return r;
        };

        int bestScore = -100000, bestRoot = -1;
        ChordAnalysis best;

        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const int rootPc = (bassPc + attempt) % 12;
            if (((pcMask >> rootPc) & 1u) == 0)
                continue;

            auto a = analyseIntervals (rotate (pcMask, rootPc));
            if (! a.recognised)
                continue;

            // Preferencia por el bajo como fundamental. Si no lo es, una inversión
            // (bajo = 3ª, 5ª o 7ª) se penaliza poco; una tensión en el bajo, mucho
            // (así C-F-G-Bb-D es "C9sus4" y no "Gm7(11)/C").
            int score = a.score;
            if (attempt == 0)
                score += 3;
            else
            {
                const int bassInterval = (bassPc - rootPc + 12) % 12;
                const bool bassIsChordTone = bassInterval == a.thirdInterval
                                          || bassInterval == a.fifthInterval
                                          || bassInterval == a.seventhInterval;
                score -= bassIsChordTone ? 1 : 5;
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestRoot = rootPc;
                best = a;
            }
        }

        if (bestRoot < 0)
            return id;

        id.valid = true;
        id.rootPc = bestRoot;
        id.suffix = best.suffix;
        id.spaced = best.spaced;
        id.thirdInterval = best.thirdInterval;
        id.fifthInterval = best.fifthInterval;
        id.seventhInterval = best.seventhInterval;
        return id;
    }

    // Cifrado del acorde en tiempo real (hilo de mensajes; asigna un juce::String).
    // Ejemplos: "F Maj", "Dm", "E dim", "Gm7", "F Maj7", "F Maj9", "Gm9", "Dm11",
    //           "C13", "Bb Maj9#11", "Em7b5(11)", "Cadd9", "Dsus4", "C Maj7/E".
    juce::String getChordName (const std::vector<int>& chordNotes) const
    {
        if (chordNotes.empty())
            return {};

        const bool flats = prefersFlats();
        const auto id = identifyChord (chordNotes);
        const int bass = *std::min_element (chordNotes.begin(), chordNotes.end());

        if (id.distinctPitchClasses == 1)
            return chordNotes.size() == 1 ? getNoteName (bass)
                                          : getPitchClassName (bass, flats) + " (8va)";

        if (id.valid)
        {
            juce::String name = getPitchClassName (id.rootPc, flats) + (id.spaced ? " " : "") + id.suffix;
            if (id.rootPc != id.bassPc)
                name << "/" << getPitchClassName (id.bassPc, flats);
            return name;
        }

        return listPitchClasses (chordNotes);
    }

    // Cifrado compacto para la paleta y la progresión: "C", "Dm7", "Bdim", "FMaj7"
    juce::String getShortChordName (const std::vector<int>& chordNotes) const
    {
        if (chordNotes.empty())
            return {};

        const bool flats = prefersFlats();
        const auto id = identifyChord (chordNotes);
        if (id.distinctPitchClasses == 1)
            return getPitchClassName (id.bassPc, flats);
        if (! id.valid)
            return listPitchClasses (chordNotes);

        juce::String suffix = id.suffix == "Maj" ? juce::String() : id.suffix;
        juce::String name = getPitchClassName (id.rootPc, flats) + suffix;
        if (id.rootPc != id.bassPc)
            name << "/" << getPitchClassName (id.bassPc, flats);
        return name;
    }

    // Grado en números romanos respecto a la tónica y la escala activas:
    // "I", "ii", "iii7", "IVΔ7", "V7", "vi", "vii°", "viiø7", "bVII", "IVsus4"...
    juce::String getRomanNumeral (const std::vector<int>& chordNotes) const
    {
        if (chordNotes.empty())
            return {};

        const auto id = identifyChord (chordNotes);
        const int rootPc = id.valid ? id.rootPc : id.bassPc;

        static const char* numerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        juce::String degree;

        if (numActivePitchClasses == 7)
        {
            // Grado dentro de la escala (I..VII); fuera de ella, alterado (b / #)
            int indexOf[12];
            int idx = 0;
            for (int i = 0; i < 12; ++i)
                indexOf[i] = -1;
            for (int i = 0; i < 12; ++i)
            {
                const int pc = (rootKey + i) % 12;
                if (isPitchClassActive (pc))
                    indexOf[pc] = idx++;
            }

            if (indexOf[rootPc] >= 0)
                degree = numerals[indexOf[rootPc]];
            else if (indexOf[(rootPc + 1) % 12] >= 0)
                degree = juce::String::fromUTF8 ("\xe2\x99\xad") + numerals[indexOf[(rootPc + 1) % 12]];
            else if (indexOf[(rootPc + 11) % 12] >= 0)
                degree = juce::String::fromUTF8 ("\xe2\x99\xaf") + numerals[indexOf[(rootPc + 11) % 12]];
        }

        if (degree.isEmpty())
        {
            // Escalas no heptatónicas: grado respecto a la escala mayor de la tónica
            static const char* chromatic[12] = { "I", "\xe2\x99\xadII", "II", "\xe2\x99\xadIII", "III", "IV",
                                                 "\xe2\x99\xafIV", "V", "\xe2\x99\xadVI", "VI", "\xe2\x99\xadVII", "VII" };
            degree = juce::String::fromUTF8 (chromatic[(rootPc - rootKey + 12) % 12]);
        }

        if (! id.valid)
            return degree;

        // Menor o disminuido -> minúsculas; el sufijo se traduce a notación de grados
        juce::String suffix = id.suffix;
        const bool minorThird = id.thirdInterval == 3;
        if (minorThird)
            degree = degree.toLowerCase().replace ("b", juce::String::fromUTF8 ("\xe2\x99\xad"));

        if (suffix == "Maj")                     suffix = {};
        else if (suffix == "dim")                suffix = juce::String::fromUTF8 ("\xc2\xb0");
        else if (suffix == "dim7")               suffix = juce::String::fromUTF8 ("\xc2\xb0" "7");
        else if (suffix.startsWith ("m7b5"))     suffix = juce::String::fromUTF8 ("\xc3\xb8" "7") + suffix.substring (4);
        else if (suffix == "aug")                suffix = "+";
        else if (minorThird && suffix.startsWith ("m") && ! suffix.startsWith ("mMaj"))
            suffix = suffix.substring (1);
        else if (minorThird && suffix.startsWith ("mMaj"))
            suffix = juce::String::fromUTF8 ("\xce\x94") + suffix.substring (4);

        suffix = suffix.replace ("Maj", juce::String::fromUTF8 ("\xce\x94"));
        if (suffix.startsWith ("(") && suffix.endsWith (")"))
            suffix = suffix.substring (1, suffix.length() - 1);

        return degree + suffix;
    }

    static juce::String listPitchClasses (const std::vector<int>& chordNotes)
    {
        const int bass = *std::min_element (chordNotes.begin(), chordNotes.end());
        const int bassPc = ((bass % 12) + 12) % 12;
        uint16_t pcMask = 0;
        for (int n : chordNotes)
            pcMask = (uint16_t) (pcMask | (1u << (unsigned) (((n % 12) + 12) % 12)));

        juce::String fallback;
        for (int i = 0; i < 12; ++i)
            if ((pcMask >> ((bassPc + i) % 12)) & 1u)
                fallback << (fallback.isEmpty() ? "" : "-") << getPitchClassName ((bassPc + i) % 12, false);
        return fallback;
    }

    //==========================================================================
    // Análisis funcional de un conjunto de intervalos (bit i = intervalo i sobre la fundamental)
    struct ChordAnalysis
    {
        juce::String suffix;
        bool spaced = false;
        bool recognised = false;
        int score = 0;
        int thirdInterval = -1, fifthInterval = -1, seventhInterval = -1;
    };

    static ChordAnalysis analyseIntervals (uint16_t rel)
    {
        auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
        int numPcs = 0;
        for (int i = 0; i < 12; ++i)
            numPcs += has (i) ? 1 : 0;

        ChordAnalysis a;

        const int third = has (4) ? 4 : (has (3) ? 3 : -1);
        const bool sus4 = third < 0 && has (5);
        const bool sus2 = third < 0 && ! sus4 && has (2);

        int fifth = -1;
        if (has (7))                     fifth = 7;
        else if (third == 3 && has (6))  fifth = 6;
        else if (third == 4 && has (8))  fifth = 8;
        else if (third == 4 && has (6))  fifth = 6;
        else if (third < 0 && has (6))   fifth = 6;

        int seventh = -1;
        if (has (11))                                 seventh = 11;
        else if (has (10))                            seventh = 10;
        else if (third == 3 && fifth == 6 && has (9)) seventh = 9;   // dim7

        // Tensiones (intervalos no usados como tonos del acorde)
        const bool b9  = has (1);
        const bool n9  = has (2) && ! sus2;
        const bool s9  = has (3) && third == 4;
        const bool n11 = has (5) && ! sus4;
        const bool s11 = has (6) && fifth != 6;
        const bool b13 = has (8) && fifth != 8;
        const bool n13 = has (9) && seventh != 9;

        juce::String s;
        bool spaced = false;

        if (third == 4)
        {
            if (seventh == 11)
            {
                s = n13 ? "Maj13" : (n9 ? "Maj9" : "Maj7");
                spaced = true;
                if (fifth == 8) s << "#5";
                if (fifth == 6) s << "b5";
            }
            else if (seventh == 10)
            {
                s = n13 ? "13" : (n9 ? "9" : "7");
                if (fifth == 8) s << "#5";
                if (fifth == 6) s << "b5";
            }
            else
            {
                if (fifth == 8 && ! n9 && ! n13) { s = "aug"; spaced = true; }
                else if (n13 && n9)              s = "6/9";
                else if (n13)                    s = "6";
                else if (n9)                     s = "add9";
                else if (n11)                    s = "add11";
                else if (fifth == 6)             s = "(b5)";
                else if (fifth < 0)              s = "(no5)";
                else                             { s = "Maj"; spaced = true; }

                if (fifth == 8 && s != "aug")    s << "(#5)";
            }

            if (b9)  s << "b9";
            if (s9)  s << "#9";
            if (n11 && seventh >= 0) s << "(11)";
            if (s11) s << "#11";
            if (b13) s << "b13";
        }
        else if (third == 3)
        {
            if (fifth == 6 && seventh == 9)
            {
                s = "dim7";
            }
            else if (fifth == 6 && seventh == 10)
            {
                s = "m7b5";
                if (n9)  s << "(9)";
                if (n11) s << "(11)";
            }
            else if (seventh == 11)
            {
                s = n9 ? "mMaj9" : "mMaj7";
                if (n11) s << "(11)";
            }
            else if (seventh == 10)
            {
                if (n13 && (n9 || n11)) s = "m13";
                else if (n9 && n11)     s = "m11";
                else if (n9)            s = "m9";
                else                    s = "m7";

                if (n11 && ! n9 && ! n13) s << "(11)";
                if (n13 && ! n9 && ! n11) s << "(13)";
            }
            else
            {
                if (fifth == 6)        { s = "dim"; spaced = true; if (n11) s << "(add11)"; }
                else if (n13 && n9)    s = "m6/9";
                else if (n13)          s = "m6";
                else if (n9)           s = "m(add9)";
                else if (n11)          s = "m(add11)";
                else if (fifth < 0)    s = "m(no5)";
                else                   s = "m";
            }

            if (b9)  s << "(b9)";
            if (s11) s << "(#11)";
            if (b13) s << "(b13)";
        }
        else if (sus4 || sus2)
        {
            if (seventh == 10)
                s = sus4 ? (n13 ? "13sus4" : (has (2) ? "9sus4" : "7sus4")) : "7sus2";
            else if (seventh == 11)
            {
                s = sus4 ? "Maj7sus4" : "Maj7sus2";
                spaced = true;
            }
            else
                s = sus4 ? (has (2) ? "sus4(add9)" : "sus4") : "sus2";

            if (b9)  s << "(b9)";
            if (b13) s << "(b13)";
            if (s11) s << "(#11)";
        }
        else if (numPcs == 2 && fifth == 7)
        {
            s = "5";
        }
        else
        {
            return a;   // sin lectura coherente con esta fundamental
        }

        int score = 0;
        score += third >= 0 ? 8 : ((sus4 || sus2) ? 3 : 0);
        score += fifth == 7 ? 4 : (fifth >= 0 ? 2 : 0);
        score += seventh >= 0 ? 2 : 0;
        score -= 3 * ((b9 ? 1 : 0) + (s9 ? 1 : 0) + (b13 ? 1 : 0));
        score -= s11 ? 1 : 0;
        score -= (n11 && third == 4) ? 2 : 0;   // 11ª natural sobre 3ª mayor (avoid note)

        a.suffix = s;
        a.spaced = spaced;
        a.recognised = true;
        a.score = score;
        a.thirdInterval = third >= 0 ? third : (sus4 ? 5 : (sus2 ? 2 : -1));
        a.fifthInterval = fifth;
        a.seventhInterval = seventh;
        return a;
    }

private:
    //==========================================================================
    // Voces del modo COMPLEX CHORDS: grados relativos a k (k+7 = octava superior)
    struct VoiceSlot
    {
        int  candidates[4];   // grado preferido y sustitutos (sólo extensiones)
        int  numCandidates;
        int  octaveShift;     // -1 = mano izquierda (una octava abajo)
        bool isBass;          // recibe el 95 % de velocidad
        bool isExtension;     // sujeta a la regla anti-choques
    };

    static constexpr VoiceSlot kLushSlots[] =
    {
        { {  0, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: fundamental grave (d0 - 12)
        { {  4, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: quinta grave (k+4) - 12
        { {  0, 0, 0, 0 }, 1,  0, false, false },   // M.D.: fundamental (k)
        { {  2, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 3ª (k+2)
        { {  6, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 7ª (k+6)
        { { 11, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 5ª arriba (k+11), voz superior abierta
        { {  8, 10, 7, 0 }, 3, 0, false, true  },   // M.D.: 9ª (k+8) -> 11ª -> octava
    };

    static constexpr VoiceSlot kNeoSoulSlots[] =
    {
        { {  0, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: fundamental grave
        { {  4, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: quinta grave
        { {  0, 0, 0, 0 }, 1,  0, false, false },   // M.D.: fundamental (k)
        { {  2, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 3ª (k+2)
        { {  6, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 7ª (k+6)
        { {  8, 7, 0, 0 }, 2,  0, false, true  },   // M.D.: 9ª (k+8) -> octava (k+7)
        { { 10, 12, 11, 0 }, 3, 0, false, true },   // M.D.: 11ª (k+10) -> 13ª -> 5ª arriba
    };

    static constexpr VoiceSlot kCinematicSlots[] =
    {
        { {  0, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: fundamental grave
        { {  4, 0, 0, 0 }, 1, -1, true,  false },   // M.I.: quinta grave
        { {  0, 0, 0, 0 }, 1,  0, false, false },   // M.D.: fundamental (k)
        { {  9, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 10ª = 3ª arriba (k+9)
        { { 11, 0, 0, 0 }, 1,  0, false, false },   // M.D.: 12ª = 5ª arriba (k+11)
        { { 14, 0, 0, 0 }, 1,  0, false, false },   // M.D.: fundamental dos octavas arriba (k+14)
        // add9 (k+8) -> 9ª una octava más arriba (k+15, para acordes menores: queda a
        // 7ª mayor de la 3ª en vez de a un semitono) -> add11 -> octava
        { {  8, 15, 10, 7 }, 4, 0, false, true },
    };

    static const VoiceSlot* getVariationSlots (ComplexVariation v, int& numSlots) noexcept
    {
        switch (v)
        {
            case ComplexVariation::NeoSoulNineEleven:
                numSlots = (int) (sizeof (kNeoSoulSlots) / sizeof (kNeoSoulSlots[0]));
                return kNeoSoulSlots;
            case ComplexVariation::CinematicAdd9:
                numSlots = (int) (sizeof (kCinematicSlots) / sizeof (kCinematicSlots[0]));
                return kCinematicSlots;
            case ComplexVariation::LushSevenNine:
            default:
                numSlots = (int) (sizeof (kLushSlots) / sizeof (kLushSlots[0]));
                return kLushSlots;
        }
    }

    // Escalas no heptatónicas: intervalos preferidos (semitonos) para cada función de grado
    struct FunctionPrefs { int intervals[4]; int count; };
    static constexpr FunctionPrefs kFunctionPrefs[7] =
    {
        { { 0, 0, 0, 0 }, 1 },    // fundamental
        { { 2, 1, 3, 0 }, 3 },    // 2ª / 9ª
        { { 4, 3, 5, 2 }, 4 },    // 3ª (o sus4 / sus2)
        { { 5, 6, 4, 0 }, 3 },    // 4ª / 11ª
        { { 7, 6, 8, 0 }, 3 },    // 5ª
        { { 9, 8, 10, 0 }, 3 },   // 6ª / 13ª
        { { 10, 11, 9, 0 }, 3 },  // 7ª
    };

    static constexpr int kNoNote = -100000;

    bool isPitchClassActive (int note) const noexcept
    {
        return ((activePitchClassMask >> (((note % 12) + 12) % 12)) & 1u) != 0;
    }

    // Avanza 'steps' grados de la escala activa desde 'note' (sin límite de rango)
    int stepScale (int note, int steps) const noexcept
    {
        int n = note;
        const int dir = steps >= 0 ? 1 : -1;
        for (int s = 0; s != steps; s += dir)
        {
            do { n += dir; } while (! isPitchClassActive (n));
        }
        return n;
    }

    void applyScaleToCustomNotes() noexcept
    {
        setCustomMask (getPitchClassMask (rootKey, scaleIndex));
    }

    // Busca la primera nota de la escala a uno de los intervalos preferidos sobre 'root'
    int findIntervalInScale (int root, const int* prefs, int numPrefs) const noexcept
    {
        for (int i = 0; i < numPrefs; ++i)
        {
            const int n = root + prefs[i];
            if (n >= 0 && n < 128 && indexInScale[(size_t) n] >= 0)
                return n;
        }
        return -1;
    }

    void rebuildTables() noexcept
    {
        numActivePitchClasses = 0;
        for (bool b : customActiveNotes)
            numActivePitchClasses += b ? 1 : 0;

        // Seguridad: nunca permitir una escala vacía (se comporta como cromática)
        const bool empty = (numActivePitchClasses == 0);
        if (empty)
            numActivePitchClasses = 12;

        activePitchClassMask = 0;
        for (int i = 0; i < 12; ++i)
            if (empty || customActiveNotes[i])
                activePitchClassMask = (uint16_t) (activePitchClassMask | (1u << (unsigned) i));

        // Desviaciones microtonales por clase de altura absoluta
        centsByPitchClass.fill (0.0f);
        const auto& def = getScale (scaleIndex);
        for (int i = 0; i < def.numDegrees; ++i)
            centsByPitchClass[(size_t) ((rootKey + def.degrees[i].semitone) % 12)] = def.degrees[i].cents;

        // Lista global ordenada 0..127
        numScaleNotes = 0;
        for (int n = 0; n < 128; ++n)
        {
            if (empty || customActiveNotes[n % 12])
            {
                indexInScale[(size_t) n] = numScaleNotes;
                scaleNotes[(size_t) numScaleNotes++] = n;
            }
            else
            {
                indexInScale[(size_t) n] = -1;
            }
        }

        // Tabla de cuantización a la nota válida más cercana (empate -> abajo)
        for (int n = 0; n < 128; ++n)
        {
            if (indexInScale[(size_t) n] >= 0)
            {
                quantizedTable[(size_t) n] = n;
                continue;
            }

            int best = -1;
            for (int dist = 1; dist < 128 && best < 0; ++dist)
            {
                const int below = n - dist;
                const int above = n + dist;
                if (below >= 0 && indexInScale[(size_t) below] >= 0)       best = below;
                else if (above < 128 && indexInScale[(size_t) above] >= 0) best = above;
            }
            quantizedTable[(size_t) n] = best >= 0 ? best : n;
        }
    }

    //==========================================================================
    int rootKey = 0;
    int scaleIndex = 0;
    float referencePitchHz = 440.0f;
    OperationMode mode = OperationMode::SmartChord;
    ChordExtension extension = ChordExtension::Triad;
    ComplexVariation complexVariation = ComplexVariation::LushSevenNine;
    uint16_t activePitchClassMask = 0b101010110101;

    // Personalización manual de la octava (se modifica sólo vía setNoteActive /
    // toggleNote / setCustomMask para que las tablas se reconstruyan siempre).
    bool customActiveNotes[12] = { true, false, true, false, true, true, false, true, false, true, false, true };

    int numActivePitchClasses = 7;
    std::array<float, 12> centsByPitchClass {};
    std::array<int, 128> scaleNotes {};
    std::array<int, 128> indexInScale {};
    std::array<int, 128> quantizedTable {};
    int numScaleNotes = 0;
};

} // namespace smartkeys
