/*
  ==============================================================================

    PluginEditor.h
    SmartKeys — Interfaz gráfica vectorial (850 x 560 px).

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
}

juce::Font makeFont (float height, bool bold = true);

// Rectángulo redondeado con brillo rosa (selección activa)
void drawGlowBox (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour fill, bool glow);

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
    float level = -100.0f, gate = -40.0f, displayLevel = -100.0f;
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

// Propiedad de componente para fijar el tamaño de letra de un botón concreto
inline const juce::Identifier fontSizeProperty { "skFontSize" };

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
    void setRefPitch (float hz);

    SmartKeysAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    smartkeys::ui::SmartKeysLookAndFeel lookAndFeel;

    // Cabecera
    smartkeys::ui::ChoiceButtons modeSwitch;
    juce::TextButton pianoButton, micButton, microButton;
    juce::Slider gainSlider, gateSlider, tuningSlider;
    juce::TextButton tune440 { "440" }, tune442 { "442" };
    smartkeys::ui::LevelMeter micMeter;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pianoAttach, micAttach, microAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttach, gateAttach, tuningAttach;

    // Módulos
    smartkeys::ui::KeySelector keySelector;
    smartkeys::ui::ScaleSelector scaleSelector;
    smartkeys::ui::ScaleBar scaleBar;
    smartkeys::ui::PianoVisualizer piano;
    smartkeys::ui::ChordDisplay chordDisplay;
    smartkeys::ui::ChoiceButtons chordTypeSwitch;     // SMART CHORDS: TRIAD / +8VA / 7TH
    smartkeys::ui::ChoiceButtons variationSwitch;     // COMPLEX CHORDS: Lush / Neo-Soul / Cinematic
    juce::TextButton panicButton { "ALL NOTES OFF" };
    int shownMode = -1, shownVariation = -1;

    juce::TooltipWindow tooltipWindow { this, 600 };

    // Motor local (hilo de mensajes) para nombres de escala y acordes
    smartkeys::ScaleChordEngine displayEngine;
    int shownRoot = -1, shownScale = -1;
    uint16_t shownMask = 0;
    uint64_t shownEvent = ~(uint64_t) 0, shownLo = ~(uint64_t) 0, shownHi = ~(uint64_t) 0;
    int shownVoiceNote = -2;

    // Teclado de la computadora (A W S E D F T G Y H U J K, Z/X = octava)
    static constexpr const char* kKeyMap = "awsedftgyhujk";
    std::array<bool, 13> computerKeysDown {};
    std::array<int, 13> computerKeyNotes {};
    int computerOctave = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessorEditor)
};
