/*
  ==============================================================================

    PluginEditor.h
    SmartKeys — Interfaz gráfica vectorial (850 x 640 px).

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace smartkeys::ui
{

//==============================================================================
// Paleta exacta
namespace Colours
{
    inline const juce::Colour background   { 0xFFF2F4F7 };
    inline const juce::Colour white        { 0xFFFFFFFF };
    inline const juce::Colour border       { 0xFFD0D5DD };
    inline const juce::Colour text         { 0xFF1D2939 };
    inline const juce::Colour charcoal     { 0xFF323639 };
    inline const juce::Colour accent       { 0xFFEC1368 };
    inline const juce::Colour disabled     { 0xFF98A2B3 };
    inline const juce::Colour label        { 0xFF8A93A6 };
    inline const juce::Colour softPanel    { 0xFFF7F8FA };
    inline const juce::Colour ledBack      { 0xFF1B1D21 };
    inline const juce::Colour warning      { 0xFFD92D20 };
}

juce::Font makeFont (float height, bool bold = true);

// Rectángulo redondeado con brillo rosa (selección activa)
void drawGlowBox (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour fill, bool glow);

// Propiedad de componente para fijar el tamaño de letra de un botón concreto
inline const juce::Identifier fontSizeProperty { "skFontSize" };

//==============================================================================
class SmartKeysLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SmartKeysLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getPopupMenuFont() override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

//==============================================================================
// Grupo de botones exclusivos enlazado a un parámetro de elección
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels);
    void resized() override;

    void setButtonFontSize (float size);
    void setTooltips (const juce::StringArray& tooltips);

private:
    void setSelected (int index);

    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// Módulo KEY: 5 bloques oscuros (#) y 7 blancos, la tónica activa en magenta
class KeySelector : public juce::Component
{
public:
    explicit KeySelector (juce::RangedAudioParameter& rootParam);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getKeyBounds (int pitchClass) const;
    int hitTestKey (juce::Point<float>) const;

    int selected = 0;
    int hovered = -1;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// Menú "Más escalas" con submenús Occidentales / Árabes (maqam) / Indias (raga)
void showScaleMenu (juce::Component& target, juce::RangedAudioParameter& scaleParam, int currentScale);

// Módulo SCALE: desplegable + botones Major / Minor / Chromatic
class ScaleSelector : public juce::Component
{
public:
    explicit ScaleSelector (juce::RangedAudioParameter& scaleParam);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MenuBox : public juce::Component
    {
    public:
        explicit MenuBox (ScaleSelector& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    private:
        ScaleSelector& owner;
    };

    void setSelected (int scaleIndex);

    juce::RangedAudioParameter& scaleParam;
    MenuBox menuBox { *this };
    juce::TextButton majorButton { "Major" }, minorButton { "Minor" }, chromaticButton { "Chromatic" };
    int selected = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// Barra horizontal ESCALA: 12 bloques cromáticos activables con clic
class ScaleBar : public juce::Component
{
public:
    ScaleBar (SmartKeysAudioProcessor& p, juce::RangedAudioParameter& scaleParam);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setState (uint16_t mask, const juce::String& scaleTitle, bool customised, int scaleIndex);

private:
    juce::Rectangle<float> getTitleBounds() const;
    juce::Rectangle<float> getBlockBounds (int pitchClass) const;

    SmartKeysAudioProcessor& processor;
    juce::RangedAudioParameter& scaleParam;
    uint16_t mask = 0;
    juce::String title { "C Major" };
    bool isCustom = false;
    int currentScale = 0;
    int hovered = -1;
};

//==============================================================================
// Medidor LED de entrada de micrófono
class LevelMeter : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setState (float levelDb, float gateDb, bool gateOpen, bool enabled);

private:
    float level = -100.0f, gate = -48.0f, displayLevel = -100.0f;
    bool open = false, active = false;
};

//==============================================================================
// Mini piano visualizador de 5 octavas (C2 – B6), interactivo
class PianoVisualizer : public juce::Component
{
public:
    static constexpr int kLowestNote = 36;   // C2
    static constexpr int kNumOctaves = 5;
    static constexpr int kHighestNote = kLowestNote + kNumOctaves * 12 - 1;   // B6

    explicit PianoVisualizer (SmartKeysAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setActiveNotes (uint64_t lo, uint64_t hi);
    void setScaleMask (uint16_t mask)    { if (mask != scaleMask) { scaleMask = mask; repaint(); } }

private:
    static bool isBlack (int note) noexcept;
    bool isNoteLit (int note) const noexcept;
    juce::Rectangle<float> getKeyRect (int note) const;
    int noteAt (juce::Point<float> p, int& velocity) const;

    SmartKeysAudioProcessor& processor;
    uint64_t activeLo = 0, activeHi = 0;
    uint16_t scaleMask = 0;
    int mouseNote = -1;
};

//==============================================================================
// Pantalla LED del acorde / nota
class ChordDisplay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setText (const juce::String& main, const juce::String& detail, bool lit);

private:
    juce::String mainText { "--" }, detailText { "Toca una tecla o canta" };   // detalle: hasta 2 líneas ("\n")
    bool isLit = false;
};

//==============================================================================
// [ DRAG CHORDS TO PIANO ROLL ]: arrastra la progresión grabada (todas las
// notas de cada acorde) como archivo .mid al Piano Roll; un clic la guarda
// y la muestra en el Finder / Explorador.
class DragChordsButton : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    DragChordsButton (SmartKeysAudioProcessor& p, std::function<juce::String()> baseNameProvider);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setCount (int numChords, bool isFull);

private:
    juce::File exportNow();

    SmartKeysAudioProcessor& processor;
    std::function<juce::String()> makeBaseName;
    int count = 0;
    bool full = false, hover = false, pressed = false, dragStarted = false;
    juce::String flashText;
    juce::uint32 flashUntilMs = 0;
};

} // namespace smartkeys::ui

//==============================================================================
class SmartKeysAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor&);
    ~SmartKeysAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void updateDisplay();
    void updateMicStatus();
    void refreshDeviceList();
    void applyDeviceSelection();
    void setRefPitch (float hz);
    juce::String makeExportBaseName() const;

    SmartKeysAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    smartkeys::ui::SmartKeysLookAndFeel lookAndFeel;

    // Fila 1: modo + salida
    smartkeys::ui::ChoiceButtons modeSwitch;
    smartkeys::ui::ChoiceButtons outputSwitch;

    // Fila 2: afinación, canal MIDI, microafinación, pánico, ganancia
    juce::Slider tuningSlider, gainSlider, gateSlider;
    juce::TextButton tune440 { "440" }, tune442 { "442" };
    juce::ComboBox midiChannelBox;
    juce::TextButton microButton, panicButton { "ALL NOTES OFF" };

    // Tira MIC: controlador de voz, fuente, dispositivo, canal, medidor, gate, estado
    juce::TextButton micButton;
    smartkeys::ui::ChoiceButtons sourceSwitch;
    juce::ComboBox deviceBox, channelBox;
    juce::TextButton rescanButton { "RESCAN" };
    smartkeys::ui::LevelMeter micMeter;
    juce::Label micStatusLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> micAttach, microAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttach, gateAttach, tuningAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiChannelAttach;

    // Módulos
    smartkeys::ui::KeySelector keySelector;
    smartkeys::ui::ScaleSelector scaleSelector;
    smartkeys::ui::ScaleBar scaleBar;
    smartkeys::ui::PianoVisualizer piano;
    smartkeys::ui::ChordDisplay chordDisplay;
    smartkeys::ui::ChoiceButtons chordTypeSwitch;     // SMART CHORDS: TRIAD / +8VA / 7TH
    smartkeys::ui::ChoiceButtons variationSwitch;     // COMPLEX CHORDS: Lush / Neo-Soul / Cinematic

    // Grabador de acordes
    smartkeys::ui::DragChordsButton dragButton;
    juce::TextButton quantizeButton { "QUANTIZE 1/16" }, clearButton { "CLEAR" };

    // Motor local (hilo de mensajes) para nombres de escala y acordes
    smartkeys::ScaleChordEngine displayEngine;
    int shownRoot = -1, shownScale = -1, shownMode = -1, shownVariation = -1;
    uint16_t shownMask = 0;
    uint64_t shownEvent = ~(uint64_t) 0, shownLo = ~(uint64_t) 0, shownHi = ~(uint64_t) 0;
    int shownVoiceNote = -2;
    juce::StringArray shownDevices;
    bool autoDeviceAttempted = false;
    int stateSyncCounter = 0;   // re-sincroniza los selectores con el proyecto cargado (~4 veces/s)

    // Teclado de la computadora (A W S E D F T G Y H U J K, Z/X = octava)
    static constexpr const char* kKeyMap = "awsedftgyhujk";
    std::array<bool, 13> computerKeysDown {};
    std::array<int, 13> computerKeyNotes {};
    int computerOctave = 4;

    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessorEditor)
};
