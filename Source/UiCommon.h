/*
  ==============================================================================

    UiCommon.h
    SmartKeys — Paleta, tipografía, LookAndFeel y controles compartidos por
    todas las piezas de la interfaz.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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

    // Funciones armónicas (paleta de acordes y carril de progresión)
    inline const juce::Colour chordMajor     { 0xFF1F6F8B };
    inline const juce::Colour chordMinor     { 0xFF6B3FA0 };
    inline const juce::Colour chordDim       { 0xFF5D6B7E };
    inline const juce::Colour chordOther     { 0xFF8A6D3B };
    inline const juce::Colour laneBackground { 0xFF1E2230 };
    inline const juce::Colour recording      { 0xFFE5322D };
}

juce::Font makeFont (float height, bool bold = true);

// Rectángulo redondeado con brillo rosa (selección activa)
void drawGlowBox (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour fill, bool glow);

// Propiedad de componente para fijar el tamaño de letra de un botón concreto
inline const juce::Identifier fontSizeProperty { "skFontSize" };

// Texto UTF-8 (acentos, ñ, ▲ ▼, ♭ ♯ °) en literales del código
inline juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

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
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
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

} // namespace smartkeys::ui
