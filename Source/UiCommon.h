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
// Enlace parámetro <-> control SIN escuchar al parámetro.
//
// juce::ParameterAttachment se registra como oyente del parámetro: cuando el
// host automatiza un parámetro, JUCE lo avisa DESDE EL HILO DE AUDIO y el enlace
// publica un mensaje al hilo de mensajes (triggerAsyncUpdate). En FL Studio para
// Mac eso pasa por su puente con el hilo principal. Aquí, en cambio, el hilo de
// mensajes LEE el valor (atómico) 30 veces por segundo: el audio nunca llama a
// la interfaz ni al MessageManager.
class PolledParameterAttachment : private juce::Timer
{
public:
    PolledParameterAttachment (juce::RangedAudioParameter& p, std::function<void (float)> onChange, juce::UndoManager* = nullptr)
        : parameter (p), setValue (std::move (onChange))
    {
        startTimerHz (30);
    }

    ~PolledParameterAttachment() override   { stopTimer(); }

    void sendInitialUpdate()
    {
        lastNormalised = parameter.getValue();
        if (setValue != nullptr)
            setValue (parameter.convertFrom0to1 (lastNormalised));
    }

    void setValueAsCompleteGesture (float newDenormalisedValue)
    {
        const auto v = parameter.convertTo0to1 (newDenormalisedValue);
        if (juce::approximatelyEqual (parameter.getValue(), v))
            return;
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (v);
        parameter.endChangeGesture();
        timerCallback();   // como JUCE: el control refleja el valor al instante
    }

    void beginGesture()   { parameter.beginChangeGesture(); }
    void endGesture()     { parameter.endChangeGesture(); }

    void setValueAsPartOfGesture (float newDenormalisedValue)
    {
        const auto v = parameter.convertTo0to1 (newDenormalisedValue);
        if (! juce::approximatelyEqual (parameter.getValue(), v))
        {
            parameter.setValueNotifyingHost (v);
            timerCallback();
        }
    }

private:
    // Hilo de mensajes: ¿cambió el parámetro (el host, la automatización o este control)?
    void timerCallback() override
    {
        const float now = parameter.getValue();
        if (! juce::approximatelyEqual (now, lastNormalised))
        {
            lastNormalised = now;
            if (setValue != nullptr)
                setValue (parameter.convertFrom0to1 (now));
        }
    }

    juce::RangedAudioParameter& parameter;
    std::function<void (float)> setValue;
    float lastNormalised = -1.0f;
};

// Equivalentes de los Slider/ComboBox/Button attachments de JUCE, con sondeo.
//
// Cada enlace guarda su control con un Component::SafePointer: si el control
// llegara a destruirse antes que el enlace (orden de los miembros del editor),
// el enlace lo sabe y no lo toca. En la 1.2–1.3 la perilla STRUM se destruía
// antes que su enlace y FL Studio se cerraba al cerrar la ventana del plugin.
class PolledSliderAttachment : private juce::Slider::Listener
{
public:
    PolledSliderAttachment (juce::RangedAudioParameter& param, juce::Slider& s)
        : slider (&s), attachment (param, [this] (float f) { setValue (f); })
    {
        s.valueFromTextFunction = [&param] (const juce::String& text) { return (double) param.convertFrom0to1 (param.getValueForText (text)); };
        s.textFromValueFunction = [&param] (double value) { return param.getText (param.convertTo0to1 ((float) value), 0); };
        s.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));

        auto range = param.getNormalisableRange();
        auto from0To1 = [range] (double start, double end, double normalised) mutable
        {
            range.start = (float) start; range.end = (float) end;
            return (double) range.convertFrom0to1 ((float) normalised);
        };
        auto to0To1 = [range] (double start, double end, double mapped) mutable
        {
            range.start = (float) start; range.end = (float) end;
            return (double) range.convertTo0to1 ((float) mapped);
        };
        auto snap = [range] (double start, double end, double mapped) mutable
        {
            range.start = (float) start; range.end = (float) end;
            return (double) range.snapToLegalValue ((float) mapped);
        };
        juce::NormalisableRange<double> newRange { (double) range.start, (double) range.end,
                                                   std::move (from0To1), std::move (to0To1), std::move (snap) };
        newRange.interval = range.interval;
        newRange.skew = range.skew;
        newRange.symmetricSkew = range.symmetricSkew;
        s.setNormalisableRange (newRange);

        attachment.sendInitialUpdate();
        s.valueChanged();
        s.addListener (this);
    }

    ~PolledSliderAttachment() override
    {
        if (auto* s = slider.getComponent())
            s->removeListener (this);
    }

private:
    void setValue (float v)
    {
        if (auto* s = slider.getComponent())
        {
            const juce::ScopedValueSetter<bool> svs (ignoreCallbacks, true);
            s->setValue (v, juce::sendNotificationSync);
        }
    }
    void sliderValueChanged (juce::Slider* s) override { if (! ignoreCallbacks) attachment.setValueAsPartOfGesture ((float) s->getValue()); }
    void sliderDragStarted (juce::Slider*) override    { attachment.beginGesture(); }
    void sliderDragEnded (juce::Slider*) override      { attachment.endGesture(); }

    juce::Component::SafePointer<juce::Slider> slider;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
};

class PolledComboBoxAttachment : private juce::ComboBox::Listener
{
public:
    PolledComboBoxAttachment (juce::RangedAudioParameter& param, juce::ComboBox& c)
        : comboBox (&c), parameter (param), attachment (param, [this] (float f) { setValue (f); })
    {
        attachment.sendInitialUpdate();
        c.addListener (this);
    }

    ~PolledComboBoxAttachment() override
    {
        if (auto* c = comboBox.getComponent())
            c->removeListener (this);
    }

private:
    void setValue (float v)
    {
        auto* c = comboBox.getComponent();
        if (c == nullptr)
            return;
        const auto index = juce::roundToInt (parameter.convertTo0to1 (v) * (float) (c->getNumItems() - 1));
        if (index == c->getSelectedItemIndex())
            return;
        const juce::ScopedValueSetter<bool> svs (ignoreCallbacks, true);
        c->setSelectedItemIndex (index, juce::sendNotificationSync);
    }

    void comboBoxChanged (juce::ComboBox* c) override
    {
        if (ignoreCallbacks)
            return;
        const auto numItems = c->getNumItems();
        const auto normalised = numItems > 1 ? (float) c->getSelectedItemIndex() / (float) (numItems - 1) : 0.0f;
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
    }

    juce::Component::SafePointer<juce::ComboBox> comboBox;
    juce::RangedAudioParameter& parameter;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
};

class PolledButtonAttachment : private juce::Button::Listener
{
public:
    PolledButtonAttachment (juce::RangedAudioParameter& param, juce::Button& b)
        : button (&b), attachment (param, [this] (float f) { setValue (f); })
    {
        attachment.sendInitialUpdate();
        b.addListener (this);
    }

    ~PolledButtonAttachment() override
    {
        if (auto* b = button.getComponent())
            b->removeListener (this);
    }

private:
    void setValue (float v)
    {
        if (auto* b = button.getComponent())
        {
            const juce::ScopedValueSetter<bool> svs (ignoreCallbacks, true);
            b->setToggleState (v >= 0.5f, juce::sendNotificationSync);
        }
    }

    void buttonClicked (juce::Button* b) override
    {
        if (! ignoreCallbacks)
            attachment.setValueAsCompleteGesture (b->getToggleState() ? 1.0f : 0.0f);
    }

    juce::Component::SafePointer<juce::Button> button;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
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
    PolledParameterAttachment attachment;
};

} // namespace smartkeys::ui
