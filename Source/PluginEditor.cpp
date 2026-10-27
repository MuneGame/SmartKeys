/*
  ==============================================================================

    PluginEditor.cpp
    SmartKeys (MIWN) — Interfaz gráfica vectorial (850 x 658 px, redimensionable).

  ==============================================================================
*/

#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace smartkeys::ui
{

//==============================================================================
juce::Font makeFont (float height, bool bold)
{
   #if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
   #endif
}

static float textWidth (const juce::Font& font, const juce::String& text)
{
   #if JUCE_MAJOR_VERSION >= 8
    return juce::GlyphArrangement::getStringWidth (font, text);
   #else
    return font.getStringWidthFloat (text);
   #endif
}

void drawGlowBox (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour fill, bool glow)
{
    if (glow)
    {
        for (int i = 4; i >= 1; --i)
        {
            const float grow = (float) i * 1.6f;
            g.setColour (Colours::accent.withAlpha (0.07f * (float) (5 - i)));
            g.fillRoundedRectangle (r.expanded (grow), corner + grow);
        }
    }

    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
}

//==============================================================================
// Logotipo de MIWN. Diseño en una cuadrícula de 34 × 34:
//   - placa redondeada (radio 9) con degradado rosa #FF3584 -> #D60C5C (acento
//     #EC1368 en el centro) y un brillo rosa suave alrededor;
//   - una onda armónica blanca que dibuja la "M" (cuatro curvas de Bézier);
//   - cinco barras de nivel escalonadas (blanco al 75 %): el flujo del estudio.
void drawMiwnLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const float s = size / 34.0f;
    const auto origin = area.withSizeKeepingCentre (size, size).getTopLeft();
    const auto toArea = juce::AffineTransform::scale (s).translated (origin.x, origin.y);

    // Brillo
    for (int i = 3; i >= 1; --i)
    {
        const float grow = (float) i * 1.3f * s;
        g.setColour (Colours::accent.withAlpha (0.055f * (float) (4 - i)));
        g.fillRoundedRectangle (juce::Rectangle<float> (origin.x, origin.y + 1.0f * s, size, size).expanded (grow),
                                9.0f * s + grow);
    }

    // Placa
    juce::Path plate;
    plate.addRoundedRectangle (0.0f, 0.0f, 34.0f, 34.0f, 9.0f);
    plate.applyTransform (toArea);
    const auto b = plate.getBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xFFFF3584), b.getTopLeft(),
                                             juce::Colour (0xFFD60C5C), b.getBottomRight(), false));
    g.fillPath (plate);

    // Onda armónica en forma de "M"
    juce::Path wave;
    wave.startNewSubPath (6.5f, 22.0f);
    wave.cubicTo (8.5f, 22.0f, 9.6f, 8.0f, 12.3f, 8.0f);
    wave.cubicTo (15.0f, 8.0f, 15.6f, 17.5f, 17.0f, 17.5f);
    wave.cubicTo (18.4f, 17.5f, 19.7f, 8.0f, 21.7f, 8.0f);
    wave.cubicTo (23.7f, 8.0f, 25.5f, 22.0f, 27.5f, 22.0f);
    wave.applyTransform (toArea);
    g.setColour (Colours::white);
    g.strokePath (wave, juce::PathStrokeType (2.8f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Barras de nivel escalonadas
    constexpr float centres[] = { 10.5f, 14.2f, 17.0f, 19.8f, 23.5f };
    constexpr float heights[] = { 2.6f, 3.8f, 5.0f, 3.8f, 2.6f };
    juce::Path bars;
    for (size_t i = 0; i < 5; ++i)
        bars.addRoundedRectangle (centres[i] - 1.1f, 28.0f - heights[i], 2.2f, heights[i], 1.1f);
    bars.applyTransform (toArea);
    g.setColour (Colours::white.withAlpha (0.75f));
    g.fillPath (bars);
}

//==============================================================================
// LookAndFeel
//==============================================================================
SmartKeysLookAndFeel::SmartKeysLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId,   Colours::white);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId,  Colours::text);
    setColour (juce::TextButton::textColourOnId,   Colours::white);

    setColour (juce::Slider::backgroundColourId,       Colours::white);
    setColour (juce::Slider::trackColourId,            Colours::accent);
    setColour (juce::Slider::textBoxTextColourId,      Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::Label::textColourId, Colours::text);

    setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xF2393D45));
    setColour (juce::PopupMenu::textColourId,                  juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId,            juce::Colour (0xFF9AA0AA));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFF2F7CF6));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setColour (juce::ComboBox::backgroundColourId, Colours::white);
    setColour (juce::ComboBox::textColourId,       Colours::text);
    setColour (juce::ComboBox::outlineColourId,    Colours::border);
    setColour (juce::ComboBox::arrowColourId,      Colours::charcoal);
}

void SmartKeysLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (3.0f);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();

    if (on)
    {
        drawGlowBox (g, r, 4.0f, (down ? Colours::accent.darker (0.15f) : Colours::accent).withMultipliedAlpha (enabled ? 1.0f : 0.35f),
                     enabled);
    }
    else
    {
        g.setColour ((down ? Colours::border.withAlpha (0.6f) : (over && enabled ? Colours::softPanel : Colours::white))
                         .withMultipliedAlpha (enabled ? 1.0f : 0.6f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Colours::border.withMultipliedAlpha (enabled ? 1.0f : 0.6f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
}

void SmartKeysLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour ((b.getToggleState() ? Colours::white : Colours::text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (5, 2), juce::Justification::centred, 1, 0.75f);
}

juce::Font SmartKeysLookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    if (const auto* size = b.getProperties().getVarPointer (fontSizeProperty))
        return makeFont ((float) static_cast<double> (*size), true);
    return makeFont (juce::jlimit (10.0f, 15.0f, (float) h * 0.42f), true);
}

void SmartKeysLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float sliderPos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);
        return;
    }

    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f, 2.0f);
    g.setColour (Colours::white);
    g.fillRoundedRectangle (r, 4.0f);

    auto fill = r.withRight (juce::jlimit (r.getX(), r.getRight(), sliderPos));
    g.setColour (Colours::accent.withAlpha (s.isEnabled() ? 0.85f : 0.3f));
    g.fillRoundedRectangle (fill, 4.0f);

    g.setColour (Colours::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void SmartKeysLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Pista completa y tramo activo
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::border);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (pos > 0.001f)
    {
        value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
        g.setColour (Colours::accent.withAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Cuerpo y aguja
    const float body = radius - 6.0f;
    g.setColour (Colours::white);
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour (Colours::border);
    g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);

    juce::Path needle;
    needle.addRoundedRectangle (-1.5f, -body + 2.0f, 3.0f, body * 0.6f, 1.5f);
    g.setColour (pos > 0.001f ? Colours::accent : Colours::charcoal);
    g.fillPath (needle, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

juce::Label* SmartKeysLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (makeFont (12.0f, true));
    l->setColour (juce::Label::textColourId, Colours::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

juce::Font SmartKeysLookAndFeel::getPopupMenuFont()
{
    return makeFont (15.0f, false);
}

void SmartKeysLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.isEnabled() ? Colours::white : Colours::softPanel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isMouseOver (true) ? Colours::accent.withAlpha (0.55f) : Colours::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    chevron.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.5f);
    g.setColour (Colours::charcoal.withAlpha (0.75f));
    g.fillPath (chevron);
}

juce::Font SmartKeysLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return makeFont (12.0f, true);
}

void SmartKeysLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, juce::jmax (10, box.getWidth() - 26), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// ChoiceButtons
//==============================================================================
ChoiceButtons::ChoiceButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels)
    : attachment (param, [this] (float v) { setSelected ((int) std::lround (v)); }, nullptr)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void ChoiceButtons::setSelected (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

void ChoiceButtons::setButtonFontSize (float size)
{
    for (auto* b : buttons)
        b->getProperties().set (fontSizeProperty, size);
    repaint();
}

void ChoiceButtons::setTooltips (const juce::StringArray& tooltips)
{
    for (int i = 0; i < buttons.size() && i < tooltips.size(); ++i)
        buttons[i]->setTooltip (tooltips[i]);
}

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    const int n = juce::jmax (1, buttons.size());
    const int w = r.getWidth() / n;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == n - 1 ? r : r.removeFromLeft (w));
}

//==============================================================================
// KeySelector
//==============================================================================
namespace
{
    constexpr int kWhiteIndex[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    // Para cada sostenido: índice de la tecla blanca a su izquierda
    constexpr int kBlackLeftWhite[12] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };

    bool isBlackPc (int pc) { return kWhiteIndex[pc] < 0; }

    juce::String keyLabel (int pc)
    {
        static const char* labels[12] = { "C", "C#\nDb", "D", "D#\nEb", "E", "F", "F#\nGb", "G", "G#\nAb", "A", "A#\nBb", "B" };
        return labels[pc];
    }

    void drawTwoLineLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size)
    {
        const auto lines = juce::StringArray::fromLines (text);
        if (lines.size() <= 1)
        {
            g.setFont (makeFont (size, true));
            g.drawText (text, r, juce::Justification::centred, false);
            return;
        }
        g.setFont (makeFont (size * 0.82f, true));
        auto top = r.withHeight (r.getHeight() * 0.5f);
        auto bottom = r.withTrimmedTop (r.getHeight() * 0.5f);
        g.drawText (lines[0], top.withTrimmedTop (top.getHeight() * 0.25f), juce::Justification::centred, false);
        g.drawText (lines[1], bottom.withTrimmedBottom (bottom.getHeight() * 0.25f), juce::Justification::centred, false);
    }
}

//==============================================================================
// Un botón de KEY: su propio componente, con sus propios límites (setBounds)
class KeySelector::KeyButton : public juce::Component
{
public:
    KeyButton (KeySelector& o, int pc) : owner (o), pitchClass (pc)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (false);   // sin brillo al pasar el ratón: sólo la tónica se ilumina
        setTitle (keyLabel (pc).replace ("\n", " / "));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool black = isBlackPc (pitchClass);

        if (owner.selected == pitchClass)
        {
            g.setColour (Colours::accent);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::white);
        }
        else if (black)
        {
            g.setColour (Colours::charcoal);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::disabled.darker (0.3f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (Colours::white);
        }
        else
        {
            g.setColour (Colours::softPanel);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (Colours::text);
        }

        drawTwoLineLabel (g, keyLabel (pitchClass), r, black ? 15.0f : 19.0f);
    }

    // Un clic = una sola elección (al pulsar; arrastrar o soltar no hacen nada)
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            owner.choose (pitchClass);
    }

private:
    KeySelector& owner;
    const int pitchClass;
};

KeySelector::KeySelector (juce::RangedAudioParameter& rootParam)
    : attachment (rootParam, [this] (float v) { setSelected (juce::jlimit (0, 11, (int) std::lround (v))); }, nullptr)
{
    for (int pc = 0; pc < 12; ++pc)
    {
        buttons[(size_t) pc] = std::make_unique<KeyButton> (*this, pc);
        addAndMakeVisible (*buttons[(size_t) pc]);
    }
    attachment.sendInitialUpdate();
}

KeySelector::~KeySelector() = default;

// Fila de abajo: 7 botones claros con 8 px entre ellos. Fila de arriba: los 5 oscuros,
// cada uno centrado sobre el hueco entre sus dos notas naturales. Entre las dos filas
// queda una franja libre; entre C#–D# y entre F#–G#–A# también.
juce::Rectangle<int> KeySelector::layoutKey (juce::Rectangle<int> area, int pc)
{
    const auto r = area.reduced (2);
    constexpr int gap = 8;
    const float whiteW = (float) (r.getWidth() - 6 * gap) / 7.0f;
    const int whiteH = juce::roundToInt ((float) r.getHeight() * 0.47f);
    const int blackW = juce::roundToInt (whiteW * 0.86f);
    const int blackH = juce::roundToInt ((float) r.getHeight() * 0.40f);

    if (! isBlackPc (pc))
    {
        const int wi = kWhiteIndex[pc];
        const int x0 = r.getX() + juce::roundToInt ((float) wi * (whiteW + (float) gap));
        const int x1 = r.getX() + juce::roundToInt ((float) wi * (whiteW + (float) gap) + whiteW);
        return { x0, r.getBottom() - whiteH, x1 - x0, whiteH };
    }

    const int left = kBlackLeftWhite[pc];
    const float centre = (float) r.getX() + (float) left * (whiteW + (float) gap) + whiteW + (float) gap * 0.5f;
    return { juce::roundToInt (centre - (float) blackW * 0.5f), r.getY() + 2, blackW, blackH };
}

juce::Rectangle<int> KeySelector::getButtonBounds (int pc) const
{
    return buttons[(size_t) juce::jlimit (0, 11, pc)]->getBounds();
}

void KeySelector::resized()
{
    for (int pc = 0; pc < 12; ++pc)
        buttons[(size_t) pc]->setBounds (layoutKey (getLocalBounds(), pc));

    // Ningún botón pisa a otro (ni un píxel)
    for (int a = 0; a < 12; ++a)
        for (int b = a + 1; b < 12; ++b)
            jassert (! buttons[(size_t) a]->getBounds().intersects (buttons[(size_t) b]->getBounds()));
}

void KeySelector::paint (juce::Graphics& g)
{
    // El brillo magenta de la tónica (detrás del botón, que pinta su propio fondo encima)
    drawGlowBox (g, getButtonBounds (selected).toFloat(), 3.0f, Colours::accent, true);
}

void KeySelector::choose (int pc)
{
    attachment.setValueAsCompleteGesture ((float) pc);   // una sola vez (si ya era esa, nada)
    setSelected (pc);
    if (onKeyChosen != nullptr)
        onKeyChosen (pc);
}

void KeySelector::setSelected (int pc)
{
    if (pc == selected)
        return;
    selected = pc;
    repaint();
    for (auto& b : buttons)
        b->repaint();
}

//==============================================================================
// Menú de escalas
//==============================================================================
void showScaleMenu (juce::Component& target, juce::RangedAudioParameter& scaleParam, int currentScale)
{
    juce::PopupMenu western, arabic, indian;

    for (int i = 0; i < ScaleChordEngine::getNumScales(); ++i)
    {
        const auto& s = ScaleChordEngine::getScale (i);
        const auto name = juce::String::fromUTF8 (s.name);
        switch (s.category)
        {
            case ScaleCategory::Western: western.addItem (i + 1, name, true, i == currentScale); break;
            case ScaleCategory::Arabic:  arabic.addItem  (i + 1, name, true, i == currentScale); break;
            case ScaleCategory::Indian:  indian.addItem  (i + 1, name, true, i == currentScale); break;
            case ScaleCategory::Basic:   break;
        }
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 ("Más escalas"));
    menu.addSeparator();
    menu.addSubMenu ("Occidentales", western);
    menu.addSubMenu (juce::String::fromUTF8 ("Árabes (maqam)"), arabic);
    menu.addSubMenu ("Indias (raga)", indian);

    auto* param = &scaleParam;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (target.getWidth()),
                        [param] (int result)
                        {
                            if (result <= 0)
                                return;
                            param->beginChangeGesture();
                            param->setValueNotifyingHost (param->convertTo0to1 ((float) (result - 1)));
                            param->endChangeGesture();
                        });
}

//==============================================================================
// ScaleSelector
//==============================================================================
ScaleSelector::ScaleSelector (juce::RangedAudioParameter& sp)
    : scaleParam (sp),
      attachment (sp, [this] (float v) { setSelected ((int) std::lround (v)); }, nullptr)
{
    addAndMakeVisible (menuBox);

    juce::TextButton* buttons[3] = { &majorButton, &minorButton, &chromaticButton };
    for (int i = 0; i < 3; ++i)
    {
        buttons[i]->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (buttons[i]);
    }

    attachment.sendInitialUpdate();
}

void ScaleSelector::setSelected (int scaleIndex)
{
    selected = scaleIndex;
    majorButton.setToggleState (scaleIndex == 0, juce::dontSendNotification);
    minorButton.setToggleState (scaleIndex == 1, juce::dontSendNotification);
    chromaticButton.setToggleState (scaleIndex == 2, juce::dontSendNotification);
    menuBox.repaint();
}

void ScaleSelector::paint (juce::Graphics& g)
{
    g.setColour (Colours::label);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("S C A L E", getLocalBounds().removeFromTop (18), juce::Justification::centredLeft, false);
}

void ScaleSelector::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (20);
    const int h = (r.getHeight() - 6) / 4;
    menuBox.setBounds (r.removeFromTop (h).reduced (3, 2));
    r.removeFromTop (2);
    majorButton.setBounds (r.removeFromTop (h));
    minorButton.setBounds (r.removeFromTop (h));
    chromaticButton.setBounds (r.removeFromTop (h));
}

void ScaleSelector::MenuBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (isMouseOver() ? Colours::softPanel : Colours::white);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    const bool fromMenu = owner.selected > 2;
    auto textArea = r.reduced (12.0f, 0.0f);
    g.setFont (makeFont (15.0f, true));
    g.setColour (fromMenu ? Colours::accent : Colours::text);
    g.drawText (fromMenu ? juce::String::fromUTF8 (ScaleChordEngine::getScale (owner.selected).name)
                         : juce::String::fromUTF8 ("Más escalas"),
                textArea.withTrimmedRight (20.0f), juce::Justification::centredLeft, true);

    juce::Path tri;
    const float cx = r.getRight() - 18.0f, cy = r.getCentreY();
    tri.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
    g.setColour (Colours::charcoal.withAlpha (0.75f));
    g.fillPath (tri);
}

void ScaleSelector::MenuBox::mouseDown (const juce::MouseEvent&)
{
    showScaleMenu (*this, owner.scaleParam, owner.selected);
}

//==============================================================================
// ScaleBar
//==============================================================================
ScaleBar::ScaleBar (SmartKeysAudioProcessor& p, juce::RangedAudioParameter& sp)
    : processor (p), scaleParam (sp)
{
}

void ScaleBar::setState (uint16_t newMask, const juce::String& scaleTitle, bool customised, int scaleIndex)
{
    if (newMask != mask || scaleTitle != title || customised != isCustom || scaleIndex != currentScale)
    {
        mask = newMask;
        title = scaleTitle;
        isCustom = customised;
        currentScale = scaleIndex;
        repaint();
    }
}

juce::Rectangle<float> ScaleBar::getTitleBounds() const
{
    return { 0.0f, 0.0f, (float) getWidth(), 22.0f };
}

juce::Rectangle<float> ScaleBar::getBlockBounds (int pc) const
{
    auto area = getLocalBounds().toFloat().withTrimmedTop (26.0f).reduced (6.0f, 5.0f);
    const float gap = 6.0f;
    const float w = (area.getWidth() - 11.0f * gap) / 12.0f;
    return { area.getX() + (float) pc * (w + gap), area.getY(), w, area.getHeight() };
}

void ScaleBar::paint (juce::Graphics& g)
{
    // Cabecera: ESCALA  <nombre> ▼
    auto head = getTitleBounds();
    g.setFont (makeFont (13.0f, true));
    g.setColour (Colours::label);
    g.drawText ("E S C A L A", head.removeFromLeft (100.0f), juce::Justification::centredLeft, false);

    const auto shown = title + (isCustom ? " (custom)" : "");
    g.setFont (makeFont (18.0f, true));
    g.setColour (Colours::accent);
   #if JUCE_MAJOR_VERSION >= 8
    const float textW = juce::GlyphArrangement::getStringWidth (makeFont (18.0f, true), shown);
   #else
    const float textW = makeFont (18.0f, true).getStringWidthFloat (shown);
   #endif
    g.drawText (shown, head.withWidth (textW + 4.0f), juce::Justification::centredLeft, false);

    juce::Path tri;
    const float cx = head.getX() + textW + 14.0f, cy = head.getCentreY();
    tri.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
    g.fillPath (tri);

    // Contenedor
    auto container = getLocalBounds().toFloat().withTrimmedTop (26.0f);
    g.setColour (juce::Colour (0xFFE4E7EC));
    g.fillRoundedRectangle (container, 4.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (container.reduced (0.5f), 4.0f, 1.0f);

    for (int pc = 0; pc < 12; ++pc)
    {
        const auto r = getBlockBounds (pc);
        const bool active = ((mask >> pc) & 1u) != 0;
        const bool hov = (pc == hovered);

        if (active)
        {
            g.setColour (hov ? Colours::softPanel : Colours::white);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (Colours::text);
            drawTwoLineLabel (g, keyLabel (pc), r.withTrimmedBottom (8.0f), 17.0f);

            g.setColour (Colours::accent);
            g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() - 6.0f).reduced (r.getWidth() * 0.08f, 0.0f), 1.5f);
        }
        else
        {
            g.setColour (hov ? Colours::disabled.brighter (0.1f) : Colours::disabled);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (Colours::white.withAlpha (0.92f));
            drawTwoLineLabel (g, keyLabel (pc), r.withTrimmedBottom (8.0f), 15.0f);
        }
    }
}

void ScaleBar::mouseDown (const juce::MouseEvent& e)
{
    auto head = getTitleBounds();
    if (head.contains (e.position) && e.position.x < 420.0f)
    {
        showScaleMenu (*this, scaleParam, currentScale);
        return;
    }

    for (int pc = 0; pc < 12; ++pc)
    {
        if (getBlockBounds (pc).contains (e.position))
        {
            processor.toggleCustomNote (pc);
            return;
        }
    }
}

void ScaleBar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int pc = 0; pc < 12; ++pc)
        if (getBlockBounds (pc).contains (e.position))
            h = pc;
    if (h != hovered) { hovered = h; repaint(); }
}

void ScaleBar::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
// LevelMeter
//==============================================================================
void LevelMeter::setState (float levelDb, float gateDb, bool gateOpen, bool enabled)
{
    // caída suave del medidor (60 FPS)
    displayLevel = levelDb > displayLevel ? levelDb : juce::jmax (levelDb, displayLevel - 1.5f);

    if (std::abs (displayLevel - level) > 0.2f || std::abs (gateDb - gate) > 0.05f || gateOpen != open || enabled != active)
    {
        level = displayLevel;
        gate = gateDb;
        open = gateOpen;
        active = enabled;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::ledBack);
    g.fillRoundedRectangle (r, 3.0f);

    constexpr int kSegments = 12;
    auto inner = r.reduced (3.0f);
    const float segW = (inner.getWidth() - (float) (kSegments - 1) * 2.0f) / (float) kSegments;
    const float norm = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 60.0f);
    const int lit = active ? (int) std::ceil (norm * (float) kSegments) : 0;

    for (int i = 0; i < kSegments; ++i)
    {
        auto seg = juce::Rectangle<float> (inner.getX() + (float) i * (segW + 2.0f), inner.getY(), segW, inner.getHeight());
        juce::Colour c = juce::Colour (0xFF3A3E45);
        if (i < lit)
            c = (i >= kSegments - 2) ? juce::Colour (0xFFFF4D4D)
                                     : (open ? Colours::accent : Colours::disabled);
        g.setColour (c);
        g.fillRoundedRectangle (seg, 1.0f);
    }

    // Marca del umbral del gate
    const float gx = inner.getX() + juce::jlimit (0.0f, 1.0f, (gate + 60.0f) / 60.0f) * inner.getWidth();
    g.setColour (juce::Colours::white.withAlpha (active ? 0.9f : 0.35f));
    g.fillRect (gx - 0.75f, r.getY() + 1.0f, 1.5f, r.getHeight() - 2.0f);
}

//==============================================================================
// PianoVisualizer
//==============================================================================
PianoVisualizer::PianoVisualizer (SmartKeysAudioProcessor& p) : processor (p) {}

bool PianoVisualizer::isBlack (int note) noexcept
{
    return isBlackPc (note % 12);
}

bool PianoVisualizer::isNoteLit (int note) const noexcept
{
    if (note < 0 || note > 127)
        return false;
    return note < 64 ? ((activeLo >> note) & 1u) != 0
                     : ((activeHi >> (note - 64)) & 1u) != 0;
}

void PianoVisualizer::setActiveNotes (uint64_t lo, uint64_t hi)
{
    if (lo != activeLo || hi != activeHi)
    {
        activeLo = lo;
        activeHi = hi;
        repaint();
    }
}

juce::Rectangle<float> PianoVisualizer::getKeyRect (int note) const
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 0.0f);   // márgenes para indicadores
    const int numWhite = kNumOctaves * 7;
    const float whiteW = r.getWidth() / (float) numWhite;

    const int octave = (note - kLowestNote) / 12;
    const int pc = note % 12;

    if (! isBlack (note))
    {
        const int wi = octave * 7 + kWhiteIndex[pc];
        return { r.getX() + (float) wi * whiteW, r.getY(), whiteW, r.getHeight() };
    }

    const int leftWhite = octave * 7 + kBlackLeftWhite[pc];
    const float bw = whiteW * 0.62f;
    return { r.getX() + (float) (leftWhite + 1) * whiteW - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.62f };
}

void PianoVisualizer::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // Teclas blancas
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (isBlack (n))
            continue;
        auto r = getKeyRect (n).reduced (0.5f, 0.0f);
        const bool lit = isNoteLit (n);
        const bool inScale = ((scaleMask >> (n % 12)) & 1u) != 0;

        g.setColour (lit ? Colours::accent : Colours::white);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (Colours::border);
        g.drawRoundedRectangle (r, 2.5f, 1.0f);

        if (! lit && inScale)
        {
            g.setColour (Colours::accent.withAlpha (0.55f));
            g.fillRect (r.getX() + r.getWidth() * 0.3f, r.getBottom() - 5.0f, r.getWidth() * 0.4f, 2.0f);
        }

        if (n % 12 == 0)
        {
            g.setColour (lit ? Colours::white : Colours::disabled);
            g.setFont (makeFont (9.0f, true));
            g.drawText ("C" + juce::String (n / 12 - 1), r.withTrimmedTop (r.getHeight() - 20.0f),
                        juce::Justification::centred, false);
        }
    }

    // Teclas negras
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (! isBlack (n))
            continue;
        auto r = getKeyRect (n);
        const bool lit = isNoteLit (n);
        const bool inScale = ((scaleMask >> (n % 12)) & 1u) != 0;

        if (lit)
            drawGlowBox (g, r, 2.0f, Colours::accent, true);
        else
        {
            g.setColour (Colours::charcoal);
            g.fillRoundedRectangle (r, 2.0f);
            if (inScale)
            {
                g.setColour (Colours::accent.withAlpha (0.7f));
                g.fillRect (r.getX() + r.getWidth() * 0.25f, r.getBottom() - 5.0f, r.getWidth() * 0.5f, 2.0f);
            }
        }
    }

    // Indicadores de notas fuera del rango visible
    bool below = false, above = false;
    for (int n = 0; n < kLowestNote; ++n)        below = below || isNoteLit (n);
    for (int n = kHighestNote + 1; n < 128; ++n) above = above || isNoteLit (n);

    auto drawArrow = [&] (bool left, bool on)
    {
        juce::Path p;
        const float cy = full.getCentreY();
        const float x = left ? full.getX() + 2.0f : full.getRight() - 2.0f;
        const float d = left ? 7.0f : -7.0f;
        p.addTriangle (x, cy, x + d, cy - 6.0f, x + d, cy + 6.0f);
        g.setColour (on ? Colours::accent : Colours::border);
        g.fillPath (p);
    };
    drawArrow (true, below);
    drawArrow (false, above);
}

int PianoVisualizer::noteAt (juce::Point<float> p, int& velocity, float edgeMargin) const
{
    // Las negras van encima de las blancas: primero las negras. Con margen, el punto tiene que
    // estar bien dentro de la tecla (no en el borde con la vecina): si no, -1
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (! isBlack (n))
            continue;
        const auto r = getKeyRect (n);
        if (r.reduced (edgeMargin, 0.0f).withTrimmedBottom (edgeMargin).contains (p))
        {
            velocity = juce::jlimit (30, 127, (int) (40.0f + 87.0f * (p.y - r.getY()) / r.getHeight()));
            return n;
        }
        if (edgeMargin > 0.0f && r.expanded (edgeMargin, 0.0f).withTrimmedBottom (-edgeMargin).contains (p))
            return -1;   // en el borde de una negra
    }
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (isBlack (n))
            continue;
        const auto r = getKeyRect (n);
        if (r.reduced (edgeMargin, 0.0f).contains (p))
        {
            velocity = juce::jlimit (30, 127, (int) (40.0f + 87.0f * (p.y - r.getY()) / r.getHeight()));
            return n;
        }
    }
    return -1;
}

void PianoVisualizer::mouseDown (const juce::MouseEvent& e)
{
    int vel = 100;
    const int n = noteAt (e.position, vel);
    if (n >= 0)
    {
        processor.pushScreenNote (n, vel, true);
        mouseNote = n;
    }
}

void PianoVisualizer::mouseDrag (const juce::MouseEvent& e)
{
    // Un temblor al pulsar (ratón o trackpad) no cambia de tecla ni dispara otro acorde
    if (mouseNote >= 0 && e.getDistanceFromDragStart() < (int) kDragSlopPx)
        return;

    int vel = 100;
    int n = noteAt (e.position, vel);
    if (mouseNote >= 0 && n >= 0 && n != mouseNote)
    {
        // Para pasar a la tecla vecina hay que entrar en ella (en el borde sigue la que suena)
        int strictVel = vel;
        if (noteAt (e.position, strictVel, kKeyEdgeMarginPx) != n)
            return;
    }

    if (n != mouseNote)
    {
        if (mouseNote >= 0)
            processor.pushScreenNote (mouseNote, 0, false);
        if (n >= 0)
            processor.pushScreenNote (n, vel, true);
        mouseNote = n;
    }
}

void PianoVisualizer::mouseUp (const juce::MouseEvent&)
{
    if (mouseNote >= 0)
        processor.pushScreenNote (mouseNote, 0, false);
    mouseNote = -1;
}

void PianoVisualizer::releaseMouseNote()
{
    if (mouseNote >= 0)
        processor.pushScreenNote (mouseNote, 0, false);
    mouseNote = -1;
}

//==============================================================================
// ChordDisplay
//==============================================================================
void ChordDisplay::setText (const juce::String& main, const juce::String& detail, bool lit)
{
    if (main != mainText || detail != detailText || lit != isLit)
    {
        mainText = main;
        detailText = detail;
        isLit = lit;
        repaint();
    }
}

//==============================================================================
// "MIDI ● SmartKeys Chords" — estado del teclado MIDI virtual
//==============================================================================
ChordPortStatus::ChordPortStatus()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChordPortStatus::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick != nullptr)
        onClick();
}

bool ChordPortStatus::hitTest (int x, int)
{
    return state != State::Hidden && (float) x >= contentLeft - 4.0f;
}

void ChordPortStatus::setState (State newState, const juce::String& deviceName)
{
    if (newState == state && deviceName == name)
        return;

    state = newState;
    name = deviceName;
    setVisible (state != State::Hidden);

    const auto quoted = "\"" + name + "\"";
    const auto clickOff = u8 ("\n(Clic aquí: elegir dónde se graban los acordes, o apagar el teclado)");
    switch (state)
    {
        case State::Active:
            setTooltip (u8 ("Teclado MIDI virtual de SmartKeys: por aquí salen los acordes completos (con su STRUM).\n"
                            "Así otro instrumento los toca y FL Studio los graba en SU Piano Roll:\n"
                            "1. Options › MIDI Settings › Input: selecciona ") + quoted
                        + u8 (", actívalo (Enable) y ponle un Port libre, p. ej. 10 (si no aparece: Refresh device list).\n"
                              "2. Clic derecho en FLEX, Serum… en el Channel Rack › Receive notes from › ") + quoted + ".\n"
                        + u8 ("3. Deja SmartKeys seleccionado, pulsa grabar y toca: cada tecla entra como el acorde completo.")
                        + clickOff);
            break;
        case State::Recording:
            setTooltip (u8 ("FL Studio está grabando y se lo dice a SmartKeys: en cada vuelta del bucle, lo que FL "
                            "vuelve a tocar del patrón de SmartKeys no sale otra vez por ") + quoted
                        + u8 (", así que no se graba doble. Lo nuevo que toques, sí.") + clickOff);
            break;
        case State::PianoRoll:
        case State::PianoRollRecording:
            setTooltip (u8 ("ACORDES AL PIANO ROLL: FL graba en el Piano Roll de SmartKeys el acorde completo, no la tecla.\n"
                            "1. Options › MIDI Settings › Input: selecciona ") + quoted
                        + u8 (" y actívalo (Enable).\n"
                              "2. Deja SmartKeys seleccionado (o clic derecho en SmartKeys › Receive notes from › ") + quoted + ").\n"
                        + u8 ("3. Toca en SmartKeys: con el ratón en su teclado, o con las teclas A W S E D F T G Y H U J K "
                              "con esta ventana delante (apaga el teclado de escritura de FL). También con la voz.\n"
                              "4. Graba. Al reproducir, SmartKeys toca las notas del Piano Roll tal cual: nada se duplica.")
                        + clickOff);
            break;
        case State::PianoRollNotReturning:
            setTooltip (u8 ("Lo que SmartKeys manda por ") + quoted
                        + u8 (" no le llega a FL, así que FL no lo graba. Options › MIDI Settings › Input: selecciona ") + quoted
                        + u8 (", actívalo (Enable) y deja SmartKeys seleccionado (o clic derecho en SmartKeys › Receive notes from › ")
                        + quoted + ")." + clickOff);
            break;
        case State::PianoRollFlKeyboard:
            setTooltip (u8 ("Estás tocando con el teclado de FL: FL graba esa tecla y SmartKeys la toca tal cual, sin acorde. "
                            "Para grabar el acorde completo, toca en SmartKeys: con el ratón en su teclado, o con las teclas "
                            "A W S E D F T G Y H U J K con esta ventana delante, y apaga el teclado de escritura de FL "
                            "(Typing keyboard to piano keyboard).") + clickOff);
            break;
        case State::Waiting:
            setTooltip (u8 ("El teclado MIDI virtual ") + quoted
                        + u8 (" aparece en cuanto el DAW pone a sonar SmartKeys (o al abrir esta ventana).") + clickOff);
            break;
        case State::Unavailable:
            setTooltip (u8 ("Otra aplicación ya tiene un teclado ") + quoted
                        + u8 (" (otra copia de FL Studio o la app SmartKeys). Se reintenta cada 3 s.") + clickOff);
            break;
        case State::Looping:
            setTooltip (u8 ("FL Studio devuelve ") + quoted
                        + u8 (" al propio SmartKeys: SmartKeys ignora ese eco, pero así ningún otro instrumento graba los acordes. "
                              "Clic derecho en FLEX, Serum… › Receive notes from › ") + quoted + "." + clickOff);
            break;
        case State::Off:
            setTooltip (u8 ("Teclado MIDI virtual ") + quoted
                        + u8 (" apagado. Clic aquí para encenderlo: por él salen los acordes completos para que otro "
                              "instrumento los toque y FL Studio los grabe en su Piano Roll."));
            break;
        case State::Hidden:
            setTooltip ({});
            break;
    }

    repaint();
}

void ChordPortStatus::paint (juce::Graphics& g)
{
    if (state == State::Hidden)
        return;

    const auto r = getLocalBounds().toFloat();
    const auto small = makeFont (8.5f, true).withExtraKerningFactor (0.12f);
    const auto main = makeFont (9.5f, true);

    const bool problem = state == State::Unavailable || state == State::Looping
                      || state == State::PianoRollNotReturning || state == State::PianoRollFlKeyboard;
    const bool pianoRoll = state == State::PianoRoll || state == State::PianoRollRecording;
    juce::String suffix;
    if (state == State::Unavailable)            suffix = u8 (" · en uso");
    if (state == State::Looping)                suffix = u8 (" · eco");
    if (state == State::Off)                    suffix = u8 (" · OFF");
    if (state == State::Recording)              suffix = u8 (" · REC");
    if (state == State::PianoRoll)              suffix = u8 (" · Piano Roll");
    if (state == State::PianoRollRecording)     suffix = u8 (" · Piano Roll · REC");
    if (state == State::PianoRollNotReturning)  suffix = u8 (" · FL no lo recibe");
    if (state == State::PianoRollFlKeyboard)    suffix = u8 (" · toca en SmartKeys");
    const bool on = state == State::Active || state == State::Recording || pianoRoll;

    const float nameW = textWidth (main, name + suffix);
    const float tagW = textWidth (small, "MIDI");
    float x = r.getRight() - nameW;

    g.setFont (main);
    auto textColour = problem ? Colours::warning
                    : (state == State::Recording || pianoRoll) ? Colours::accent
                    : (on ? Colours::charcoal : Colours::label);
    if (isMouseOver())
        textColour = problem || state == State::Recording || pianoRoll ? textColour.darker (0.3f) : Colours::accent;
    g.setColour (textColour);
    g.drawText (name + suffix, juce::Rectangle<float> (x, r.getY(), nameW + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);

    x -= 9.0f;
    const float d = 5.0f;
    const juce::Rectangle<float> dot (x, r.getCentreY() - d * 0.5f, d, d);
    if (on)
    {
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (1.5f));
        g.setColour (Colours::accent);
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (problem ? Colours::warning : Colours::label);
        g.drawEllipse (dot.reduced (0.5f), 1.0f);
    }

    x -= tagW + 5.0f;
    g.setFont (small);
    g.setColour (Colours::label);
    g.drawText ("MIDI", juce::Rectangle<float> (x, r.getY(), tagW + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
    contentLeft = x;
}

void ChordDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::ledBack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    // Rejilla LED sutil
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    for (float y = r.getY() + 3.0f; y < r.getBottom(); y += 3.0f)
        g.fillRect (r.getX() + 4.0f, y, r.getWidth() - 8.0f, 1.0f);

    const auto lines = juce::StringArray::fromLines (detailText);
    const int numLines = juce::jlimit (1, 2, lines.size());

    auto inner = r.reduced (10.0f, 6.0f);
    auto mainArea = inner.removeFromTop (inner.getHeight() - 14.0f * (float) numLines - 2.0f);

    const auto mainColour = isLit ? Colours::accent : Colours::accent.withAlpha (0.38f);
    g.setFont (makeFont (30.0f, true));
    if (isLit)
    {
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.drawFittedText (mainText, mainArea.translated (0.0f, 1.0f).toNearestInt(), juce::Justification::centred, 1, 0.55f);
    }
    g.setColour (mainColour);
    g.drawFittedText (mainText, mainArea.toNearestInt(), juce::Justification::centred, 1, 0.55f);

    g.setFont (makeFont (11.0f, true));
    for (int i = 0; i < numLines; ++i)
    {
        // Primera línea de 2: nombre de la variación (rosa tenue); última: detalle de notas
        const bool isVariationLine = (numLines == 2 && i == 0);
        g.setColour (isVariationLine ? Colours::accent.withAlpha (isLit ? 0.85f : 0.5f)
                                     : juce::Colour (0xFFB8BEC8).withAlpha (isLit ? 1.0f : 0.6f));
        auto lineArea = inner.removeFromTop (14.0f);
        g.drawFittedText (lines[i], lineArea.toNearestInt(), juce::Justification::centred, 1, 0.65f);
    }
}

//==============================================================================
// VoiceReadout: "Mic: A3 -> Acorde: Am"
//==============================================================================
bool VoiceReadout::set (Kind k, const juce::String& a, const juce::String& b, const juce::String& c, bool warn)
{
    if (k == kind && a == note && b == label && c == result && warn == warning)
        return false;

    kind = k;
    note = a;
    label = b;
    result = c;
    warning = warn;
    repaint();
    return true;
}

void VoiceReadout::showNothing()                       { set (Kind::Nothing, {}, {}, {}, false); }
void VoiceReadout::showIdle()                          { set (Kind::Idle, {}, {}, {}, false); }
void VoiceReadout::showMessage (const juce::String& m, bool isWarning) { set (Kind::Message, m, {}, {}, isWarning); }
void VoiceReadout::showVoice (const juce::String& n, const juce::String& k, const juce::String& r)
{
    set (Kind::Voice, n, k, r, false);
}

void VoiceReadout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    if (kind == Kind::Nothing)
        return;

    if (kind == Kind::Message)
    {
        g.setColour (warning ? Colours::warning : Colours::label);
        g.setFont (makeFont (11.5f, true));
        g.drawFittedText (note, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        return;
    }

    // Mic: <nota>  ->  Acorde: <acorde>
    const auto small = makeFont (11.5f, true);
    const auto big = makeFont (17.0f, true);
    float x = r.getX();

    auto draw = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour, float gapAfter)
    {
        const float w = textWidth (font, text);
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text, juce::Rectangle<float> (x, r.getY(), w + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        x += w + gapAfter;
    };

    draw ("Mic:", small, Colours::label, 6.0f);

    if (kind == Kind::Idle || note.isEmpty())
    {
        draw ("--", big, Colours::disabled, 0.0f);
        return;
    }

    draw (note, big, Colours::text, 10.0f);

    if (result.isNotEmpty())
    {
        draw ("->", small, Colours::label, 10.0f);
        draw (label + ":", small, Colours::label, 6.0f);
        draw (result, big, Colours::accent, 0.0f);
    }
}

//==============================================================================
// InstrumentSlot: "+ LOAD PLUGIN" / "Loaded: Serum" (2.0)
//==============================================================================
static void drawChevron (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto c = area.getCentre();
    juce::Path p;
    p.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
    p.lineTo (c.x, c.y + 2.5f);
    p.lineTo (c.x + 4.0f, c.y - 2.0f);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static void drawWarningIcon (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto r = area.withSizeKeepingCentre (18.0f, 16.0f);
    juce::Path tri;
    tri.addTriangle (r.getCentreX(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getBottom());
    g.setColour (Colours::warning);
    g.strokePath (tri, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRoundedRectangle (r.getCentreX() - 0.9f, r.getY() + 5.0f, 1.8f, 6.0f, 0.9f);
    g.fillEllipse (r.getCentreX() - 1.1f, r.getBottom() - 3.8f, 2.2f, 2.2f);
}

void InstrumentSlot::setState (Look newLook, const juce::String& newPrefix, const juce::String& newTitle, const juce::String& newSubtitle)
{
    if (newLook == look && newPrefix == prefix && newTitle == title && newSubtitle == subtitle)
        return;
    look = newLook;
    prefix = newPrefix;
    title = newTitle;
    subtitle = newSubtitle;
    repaint();
}

void InstrumentSlot::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

void InstrumentSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver (true);
    const bool empty = look == Look::Empty;

    g.setColour (empty ? Colours::accent.withAlpha (over ? 0.10f : 0.045f) : (over ? Colours::softPanel : Colours::white));
    g.fillRoundedRectangle (r, 6.0f);

    // Vacía: borde rosa discontinuo; con un problema: rojo
    if (empty)
    {
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (0.5f), 6.0f);
        const float dashes[] = { 5.0f, 3.5f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (Colours::accent.withAlpha (over ? 0.95f : 0.7f));
        g.fillPath (dashed);
    }
    else
    {
        g.setColour (look == Look::Problem ? Colours::warning.withAlpha (0.8f) : Colours::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    // Icono: "+" (vacía), aviso (problema) o un módulo de instrumento con tres faders
    auto icon = r.removeFromLeft (r.getHeight()).reduced (7.0f);
    if (empty)
    {
        g.setColour (Colours::accent);
        const auto c = icon.getCentre();
        g.fillRoundedRectangle (c.x - 7.0f, c.y - 1.2f, 14.0f, 2.4f, 1.2f);
        g.fillRoundedRectangle (c.x - 1.2f, c.y - 7.0f, 2.4f, 14.0f, 1.2f);
    }
    else if (look == Look::Problem)
    {
        drawWarningIcon (g, icon);
    }
    else
    {
        const auto box = icon.withSizeKeepingCentre (20.0f, 18.0f);
        const auto colour = look == Look::Loaded ? Colours::accent : Colours::disabled;
        g.setColour (colour);
        g.drawRoundedRectangle (box, 3.5f, 1.4f);
        const float levels[] = { 0.35f, 0.7f, 0.5f };
        for (int i = 0; i < 3; ++i)
        {
            const float x = box.getX() + 5.0f + (float) i * 5.0f;
            g.fillRoundedRectangle (x - 0.7f, box.getY() + 4.0f, 1.4f, box.getHeight() - 8.0f, 0.7f);
            const float y = box.getBottom() - 4.0f - levels[i] * (box.getHeight() - 8.0f);
            g.fillRoundedRectangle (x - 2.2f, y - 1.3f, 4.4f, 2.6f, 1.2f);
        }
    }

    drawChevron (g, r.removeFromRight (24.0f), empty ? Colours::accent : Colours::label);

    // Texto: título (con su "Loaded:" delante) y una línea de detalle
    auto text = r.reduced (2.0f, 3.0f);
    auto top = text.removeFromTop (text.getHeight() * 0.56f);
    const auto titleFont = makeFont (13.5f, true);
    float x = top.getX();
    if (prefix.isNotEmpty())
    {
        const auto prefixFont = makeFont (11.5f, true);
        const float w = textWidth (prefixFont, prefix);
        g.setFont (prefixFont);
        g.setColour (Colours::label);
        g.drawText (prefix, juce::Rectangle<float> (x, top.getY(), w + 2.0f, top.getHeight()), juce::Justification::bottomLeft, false);
        x += w + 6.0f;
    }
    g.setFont (titleFont);
    g.setColour (empty ? Colours::accent : look == Look::Problem ? Colours::warning
                                         : look == Look::Busy || look == Look::Bypassed ? Colours::label : Colours::text);
    g.drawFittedText (title, juce::Rectangle<float> (x, top.getY(), top.getRight() - x, top.getHeight()).toNearestInt(),
                      juce::Justification::bottomLeft, 1, 0.7f);

    g.setFont (makeFont (10.5f, true));
    g.setColour (look == Look::Problem ? Colours::warning.withAlpha (0.85f) : Colours::label);
    g.drawFittedText (subtitle, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
}

//==============================================================================
// PresetBox
//==============================================================================
void PresetBox::setName (const juce::String& newName)
{
    if (newName != name)
    {
        name = newName;
        repaint();
    }
}

void PresetBox::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

void PresetBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (isMouseOver (true) ? Colours::softPanel : Colours::white);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    drawChevron (g, r.removeFromRight (22.0f), Colours::label);
    auto text = r.reduced (9.0f, 3.0f);
    g.setFont (makeFont (8.5f, true).withExtraKerningFactor (0.2f));
    g.setColour (Colours::label);
    g.drawText ("PRESET", text.removeFromTop (text.getHeight() * 0.42f), juce::Justification::bottomLeft, false);
    g.setFont (makeFont (12.0f, true));
    g.setColour (name.isEmpty() ? Colours::disabled : Colours::text);
    g.drawFittedText (name.isEmpty() ? u8 ("Sin preset") : name, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
}

//==============================================================================
// PresetDialog: guardar / borrar un preset dentro de la interfaz
//==============================================================================
PresetDialog::PresetDialog()
{
    setWantsKeyboardFocus (true);

    nameEditor.setFont (makeFont (15.0f, true));
    nameEditor.setIndents (10, 7);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, Colours::softPanel);
    nameEditor.setColour (juce::TextEditor::textColourId, Colours::text);
    nameEditor.setColour (juce::TextEditor::outlineColourId, Colours::border);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    nameEditor.setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.25f));
    nameEditor.setColour (juce::CaretComponent::caretColourId, Colours::accent);
    nameEditor.setInputRestrictions (64);
    nameEditor.onTextChange = [this] { updateWarning(); };
    nameEditor.onReturnKey = [this] { okButton.triggerClick(); };
    nameEditor.onEscapeKey = [this] { close(); };
    addAndMakeVisible (nameEditor);

    includeInstrument.setColour (juce::ToggleButton::textColourId, Colours::text);
    includeInstrument.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    includeInstrument.setColour (juce::ToggleButton::tickDisabledColourId, Colours::border);
    addAndMakeVisible (includeInstrument);

    // El teclado se queda en el nombre: marcar "Incluir…" y pulsar Enter guarda (un botón con el
    // foco se pulsaría con Enter y desmarcaría la casilla)
    for (juce::Button* b : { (juce::Button*) &includeInstrument, (juce::Button*) &okButton, (juce::Button*) &cancelButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
    }

    okButton.setToggleState (true, juce::dontSendNotification);   // botón principal (rosa)
    okButton.getProperties().set (fontSizeProperty, 12.0f);
    cancelButton.getProperties().set (fontSizeProperty, 12.0f);
    okButton.onClick = [this]
    {
        if (deleting)
        {
            auto callback = onDelete;
            close();
            if (callback != nullptr)
                callback();
            return;
        }

        const auto name = nameEditor.getText().trim();
        if (smartkeys::presets::cleanName (name).isEmpty())
        {
            warning = u8 ("Escribe un nombre para el preset");
            repaint();
            nameEditor.grabKeyboardFocus();
            return;
        }

        auto callback = onSave;
        const bool include = includeInstrument.isEnabled() && includeInstrument.getToggleState();
        close();
        if (callback != nullptr)
            callback (name, include);
    };
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
}

juce::Rectangle<int> PresetDialog::getCard() const
{
    return getLocalBounds().withSizeKeepingCentre (430, deleting ? 160 : 214);
}

void PresetDialog::showSave (const juce::String& suggestedName, const juce::String& instrumentName)
{
    deleting = false;
    title = u8 ("GUARDAR PRESET");
    message = u8 ("Guarda KEY, SCALE, el modo, el voicing, el STRUM y MIC VOICE.");
    nameEditor.setVisible (true);
    nameEditor.setText (suggestedName, false);
    nameEditor.selectAll();
    includeInstrument.setVisible (true);
    includeInstrument.setEnabled (instrumentName.isNotEmpty());
    includeInstrument.setButtonText (instrumentName.isNotEmpty()
                                        ? u8 ("Incluir ") + instrumentName + u8 (" con su sonido (cadena favorita)")
                                        : u8 ("Incluir el instrumento (no hay ninguno cargado)"));
    includeInstrument.setToggleState (false, juce::dontSendNotification);
    okButton.setButtonText (u8 ("GUARDAR"));
    cancelButton.setButtonText (u8 ("CANCELAR"));
    updateWarning();
    setVisible (true);
    toFront (false);
    resized();
    nameEditor.grabKeyboardFocus();
}

void PresetDialog::showDelete (const juce::String& presetName)
{
    deleting = true;
    title = u8 ("BORRAR PRESET");
    message = u8 ("¿Borrar \"") + presetName + u8 ("\"? No se puede deshacer.");
    warning = {};
    nameEditor.setVisible (false);
    includeInstrument.setVisible (false);
    okButton.setButtonText (u8 ("BORRAR"));
    cancelButton.setButtonText (u8 ("CANCELAR"));
    setVisible (true);
    toFront (false);
    resized();
    grabKeyboardFocus();
}

void PresetDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! getCard().contains (e.getPosition()))
        close();
}

void PresetDialog::close()
{
    setVisible (false);
    if (auto* content = getParentComponent())
        if (auto* editor = content->getParentComponent())
            editor->grabKeyboardFocus();   // el teclado de la PC vuelve a tocar notas
}

void PresetDialog::updateWarning()
{
    const auto clean = smartkeys::presets::cleanName (nameEditor.getText());
    warning = ! deleting && smartkeys::presets::userPresetExists (clean)
                ? u8 ("Ya existe \"") + clean + u8 ("\": se reemplazará")
                : juce::String();
    repaint();
}

bool PresetDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        okButton.triggerClick();
        return true;
    }
    return true;   // ninguna tecla llega al teclado de notas mientras está abierto
}

void PresetDialog::resized()
{
    auto card = getCard().reduced (22, 18);
    card.removeFromTop (48);   // título + mensaje
    if (! deleting)
    {
        nameEditor.setBounds (card.removeFromTop (34));
        card.removeFromTop (10);
        includeInstrument.setBounds (card.removeFromTop (24));
        card.removeFromTop (22);   // aviso
    }
    auto buttons = card.removeFromBottom (34);
    okButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (120));
}

void PresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (Colours::text.withAlpha (0.38f));

    const auto card = getCard().toFloat();
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.05f * (float) (4 - i)));
        g.fillRoundedRectangle (card.expanded ((float) i * 2.0f).translated (0.0f, 2.0f), 12.0f + (float) i * 2.0f);
    }
    g.setColour (Colours::white);
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (card.getX(), card.getY(), 40.0f, 3.0f, 1.5f);

    auto area = card.reduced (22.0f, 16.0f);
    g.setColour (Colours::text);
    g.setFont (makeFont (13.0f, true).withExtraKerningFactor (0.12f));
    g.drawText (title, area.removeFromTop (20.0f), juce::Justification::centredLeft, false);
    g.setColour (Colours::label);
    g.setFont (makeFont (11.5f, true));
    g.drawFittedText (message, area.removeFromTop (26.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);

    if (warning.isNotEmpty() && ! deleting)
    {
        const auto w = includeInstrument.getBounds().toFloat().translated (0.0f, 26.0f).withHeight (18.0f);
        g.setColour (Colours::warning);
        g.setFont (makeFont (11.0f, true));
        g.drawFittedText (warning, w.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

} // namespace smartkeys::ui

//==============================================================================
// EDITOR
//==============================================================================
using namespace smartkeys::ui;

namespace
{
    // Coordenadas del contenido (850 x 658): tarjetas y filas
    constexpr float kInstCardY  = 56.0f,  kInstCardH  = 44.0f;    // 2.0: INSTRUMENT
    constexpr float kMicCardY   = 108.0f, kMicCardH   = 44.0f;
    constexpr float kKeyCardY   = 160.0f, kKeyCardH   = 170.0f;
    constexpr float kScaleBarY  = 338.0f, kScaleBarH  = 84.0f;
    constexpr float kKeyboardY  = 430.0f, kKeyboardH  = 218.0f;
    constexpr int   kMicRowShift = (int) (kMicCardY - 56.0f);     // la tira MIC bajó una fila

    // Menús de la ranura y de los presets: los instrumentos / presets van de 1 en adelante
    enum MenuIds : int { kMenuScanNew = 100000, kMenuScanAll, kMenuRetry, kMenuUnload, kMenuInfo,
                         kMenuSavePreset, kMenuDeletePreset, kMenuPresetFolder };
}

SmartKeysAudioProcessorEditor::SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      apvts (p.getValueTreeState()),
      modeSwitch (*apvts.getParameter (ParamIDs::mode), { "SMART CHORDS", "SINGLE NOTES", "COMPLEX CHORDS" }),
      keySelector (*apvts.getParameter (ParamIDs::rootKey)),
      scaleSelector (*apvts.getParameter (ParamIDs::scale)),
      scaleBar (p, *apvts.getParameter (ParamIDs::scale)),
      piano (p),
      chordTypeSwitch (*apvts.getParameter (ParamIDs::chordExt), { "TRIAD", "+8VA", "7TH" }),
      variationSwitch (*apvts.getParameter (ParamIDs::complexVar), { "LUSH 7/9", "NEO-SOUL", "CINEMATIC" }),
      strumDirSwitch (*apvts.getParameter (ParamIDs::strumDir), { u8 ("ASC ▲"), u8 ("DESC ▼") })
{
    // Todos los controles ya existen (son miembros directos): aquí sólo se
    // configuran y se añaden. setSize() va al final del constructor.
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };

    //--------------------------------------------------------------------------
    // Fila superior: modos + GAIN (la salida MIDI va siempre por el canal 1)
    modeSwitch.setButtonFontSize (12.5f);
    modeSwitch.setTooltips ({ u8 ("1 tecla = acorde diatónico (tríada / +8va / 7ª)"),
                              u8 ("1 tecla = 1 nota corregida a la escala"),
                              u8 ("1 tecla = acorde de 5 a 7 notas a dos manos (R&B / Neo-Soul)") });
    content.addAndMakeVisible (modeSwitch);

    for (auto* s : { &gainSlider, &gateSlider })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        s->setColour (juce::Slider::textBoxTextColourId, Colours::text);
        content.addAndMakeVisible (s);
    }
    gainSlider.setTooltip (u8 ("Volumen del instrumento cargado en la ranura (su sonido sale por SmartKeys). Los acordes "
                               "salen siempre también por la salida MIDI del plugin, en el canal 1 (en FL Studio: "
                               "engranaje > MIDI > Output port)."));
    gateSlider.setTooltip (u8 ("Umbral del micrófono: por debajo, la voz no dispara notas"));

    //--------------------------------------------------------------------------
    // Fila INSTRUMENT (2.0): la ranura (el instrumento que toca los acordes), su
    // ventana, BYPASS, UNLOAD y los presets de SmartKeys
    instrumentSlot.onClick = [this] { showInstrumentMenu(); };
    content.addAndMakeVisible (instrumentSlot);

    for (auto* b : { &showGuiButton, &bypassButton, &unloadButton })
    {
        b->getProperties().set (fontSizeProperty, 11.5f);
        content.addAndMakeVisible (b);
    }
    showGuiButton.setTooltip (u8 ("Abre o cierra la ventana del instrumento cargado: ahí eliges sus presets y mueves sus "
                                  "perillas. Flota junto a SmartKeys y se cierra con ella."));
    showGuiButton.onClick = [this]
    {
        auto& hosted = audioProcessor.getHostedInstrument();
        if (hosted.isWindowOpen())
            hosted.closeWindow();
        else
            hosted.showWindow (this);
        updateInstrumentRow (true);
    };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (u8 ("BYPASS: el instrumento deja de sonar (suelta todas sus notas). Los acordes siguen "
                                 "saliendo por MIDI."));
    bypassButton.onClick = [this]
    {
        audioProcessor.getHostedInstrument().setBypassed (bypassButton.getToggleState());
        updateInstrumentRow (true);
    };
    unloadButton.setTooltip (u8 ("UNLOAD: quita el instrumento de la ranura."));
    unloadButton.onClick = [this]
    {
        audioProcessor.getHostedInstrument().unload();
        updateInstrumentRow (true);
    };

    presetBox.setTooltip (u8 ("Presets de SmartKeys: KEY, SCALE, modo, voicing, STRUM y MIC VOICE (y, si quieres, el "
                              "instrumento con su sonido). Los presets del instrumento se eligen en su ventana."));
    presetBox.onClick = [this] { showPresetMenu(); };
    content.addAndMakeVisible (presetBox);

    presetDialog.onSave = [this] (const juce::String& name, bool includeInstrument)
    {
        if (auto preset = audioProcessor.createPresetXml (includeInstrument))
            if (smartkeys::presets::save (name, *preset).wasOk())
                audioProcessor.setPresetName (smartkeys::presets::cleanName (name));
        updateInstrumentRow (true);
    };

    //--------------------------------------------------------------------------
    // Tira MIC: controlador de voz en tiempo real. La voz llega por la entrada
    // del plugin si el DAW la manda; si no, SmartKeys lee el micrófono de macOS.
    micButton.setClickingTogglesState (true);
    micButton.getProperties().set (fontSizeProperty, 12.0f);
    micButton.onStateChange = [this]
    {
        const auto text = juce::String ("MIC VOICE: ") + (micButton.getToggleState() ? "ON" : "OFF");
        if (micButton.getButtonText() != text)
            micButton.setButtonText (text);
    };
   #if JUCE_MAC
    micButton.setTooltip (u8 ("Canta o tararea: tu voz, corregida a KEY y SCALE, dispara la nota o el acorde del modo "
                              "elegido en tiempo real (lo toca el instrumento cargado y sale por MIDI).\n"
                              "SmartKeys lee la entrada de audio activa de macOS (Ajustes del Sistema > Sonido > Entrada); "
                              "si el DAW le manda audio por su entrada (sidechain), usa ese."));
   #else
    micButton.setTooltip (u8 ("Canta o tararea: tu voz, corregida a KEY y SCALE, dispara la nota o el acorde del modo "
                              "elegido en tiempo real (lo toca el instrumento cargado y sale por MIDI).\n"
                              "En Windows la voz entra por la entrada sidechain del plugin."));
   #endif
    content.addAndMakeVisible (micButton);

    content.addAndMakeVisible (micMeter);
    content.addAndMakeVisible (voiceReadout);

    //--------------------------------------------------------------------------
    // KEY · SCALE · ESCALA. Elegir otra tónica suelta lo que tocaban el ratón y el teclado de
    // la PC (nada queda colgado); si una tecla de la PC sigue pulsada, vuelve a sonar en la tónica nueva
    keySelector.onKeyChosen = [this] (int) { releaseUiNotes(); };
    content.addAndMakeVisible (keySelector);
    content.addAndMakeVisible (scaleSelector);
    content.addAndMakeVisible (scaleBar);

    //--------------------------------------------------------------------------
    // LIVE KEYBOARD + acorde + voicing + STRUM
    content.addAndMakeVisible (piano);
    content.addAndMakeVisible (chordDisplay);

    chordTypeSwitch.setButtonFontSize (12.0f);
    chordTypeSwitch.setTooltips ({ u8 ("Tríada: grados k, k+2, k+4"),
                                   u8 ("Tríada + octava: añade k+7"),
                                   u8 ("Cuatriada: añade la 7ª (k+6)") });
    content.addAndMakeVisible (chordTypeSwitch);

    variationSwitch.setButtonFontSize (11.0f);
    variationSwitch.setTooltips (smartkeys::ScaleChordEngine::getComplexVariationNames());
    content.addChildComponent (variationSwitch);   // visible sólo en COMPLEX CHORDS

    strumKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    strumKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    strumKnob.setTooltip (u8 ("STRUM SPEED: retardo progresivo entre las notas del acorde (0 a 200 ms). "
                              "0 = todas juntas. Doble clic: OFF."));
    content.addAndMakeVisible (strumKnob);

    strumTitleLabel.setText ("STRUM SPEED", juce::dontSendNotification);
    strumTitleLabel.setFont (makeFont (10.5f, true));
    strumTitleLabel.setColour (juce::Label::textColourId, Colours::label);
    strumValueLabel.setFont (makeFont (17.0f, true));
    for (auto* l : { &strumTitleLabel, &strumValueLabel })
    {
        l->setBorderSize ({ 0, 2, 0, 0 });
        l->setJustificationType (juce::Justification::centredLeft);
        l->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (l);
    }

    strumDirSwitch.setButtonFontSize (11.5f);
    strumDirSwitch.setTooltips ({ u8 ("ASC ▲: las notas entran de la más grave a la más aguda"),
                                  u8 ("DESC ▼: las notas entran de la más aguda a la más grave") });
    content.addAndMakeVisible (strumDirSwitch);

    //--------------------------------------------------------------------------
    // Enlaces con los parámetros: los controles ya están configurados
    gainAttach  = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::masterGain), gainSlider);
    gateAttach  = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::micGate), gateSlider);
    strumAttach = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::strumMs), strumKnob);
    strumKnob.setDoubleClickReturnValue (true, 0.0);
    micAttach = std::make_unique<PolledButtonAttachment> (*apvts.getParameter (ParamIDs::micOn), micButton);
    micButton.onStateChange();

    setWantsKeyboardFocus (true);
    computerKeyNotes.fill (-1);

   #if JucePlugin_Build_Standalone
    // En la app Standalone JUCE silencia la entrada por defecto "para evitar
    // realimentación". SmartKeys nunca envía la voz a la salida, así que la
    // reactivamos para que el controlador vocal funcione de inmediato.
    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    // Teclado MIDI virtual: estado arriba a la derecha (clic: encender / apagar).
    // Abrir la ventana ya crea el teclado (sin esperar a que el DAW ponga a sonar el plugin).
    content.addChildComponent (portStatus);
    portStatus.onClick = [this] { showChordPortMenu(); };
    shownDawLiveNotes = audioProcessor.getDawLiveNoteCount();
    audioProcessor.markInUse();

    // Encima de todo: guardar / borrar preset (oculto)
    content.addChildComponent (presetDialog);

    // La ventana del instrumento cargado vuelve a abrirse si estaba abierta al cerrar SmartKeys
    audioProcessor.getHostedInstrument().smartKeysEditorOpened (*this);

    timerCallback();
    startTimerHz (60);

    // Ventana redimensionable (mantiene la proporción); recuerda el tamaño del proyecto.
    // La escala guardada se lee ANTES de fijar los límites: setResizeLimits() ajusta
    // el editor (aún de 0×0) al tamaño mínimo y resized() guardaría esa escala.
    const float savedScale = audioProcessor.getEditorScale();
    setResizable (true, true);
    setResizeLimits ((int) (kWidth * 0.7f), (int) (kHeight * 0.7f), kWidth * 2, kHeight * 2);
    if (auto* editorConstrainer = getConstrainer())
        editorConstrainer->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    // Última línea: todos los componentes existen y están configurados
    setSize (juce::roundToInt ((float) kWidth * savedScale), juce::roundToInt ((float) kHeight * savedScale));
}

SmartKeysAudioProcessorEditor::~SmartKeysAudioProcessorEditor()
{
    // 1) Nada más de la interfaz se ejecuta mientras la ventana se cierra
    stopTimer();

    // 2) Los enlaces se destruyen antes que los controles que usan (además ya
    //    van declarados después de ellos y guardan cada control con SafePointer)
    micAttach.reset();
    strumAttach.reset();
    gateAttach.reset();
    gainAttach.reset();

    // 3) Ninguna nota tocada con el ratón o el teclado de la PC queda colgada
    piano.releaseMouseNote();
    releaseComputerKeys();

    // 4) La ventana del instrumento cargado se cierra con la de SmartKeys (y lo recuerda)
    audioProcessor.getHostedInstrument().smartKeysEditorClosing();

    setLookAndFeel (nullptr);
}

//==============================================================================
void SmartKeysAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void SmartKeysAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Línea de acento superior
    g.setColour (Colours::accent);
    g.fillRect (0, 0, 64, 3);
    g.setColour (Colours::border);
    g.fillRect (64, 0, kWidth - 64, 1);

    // Logotipo de MIWN + título + punto de acento + "FROM MIWN"
    drawMiwnLogo (g, { 16.0f, 11.0f, 34.0f, 34.0f });

    const auto titleFont = makeFont (21.0f, true);
    const float titleWidth = textWidth (titleFont, "SMARTKEYS");
    g.setColour (Colours::text);
    g.setFont (titleFont);
    g.drawText ("SMARTKEYS", juce::Rectangle<float> (58.0f, 11.0f, titleWidth + 4.0f, 26.0f), juce::Justification::centredLeft, false);
    g.setColour (Colours::accent);
    g.fillEllipse (58.0f + titleWidth + 4.0f, 21.0f, 6.5f, 6.5f);

    {
        // Mayúsculas con un espaciado sutil: "FROM" en gris, la marca en grafito
        const auto from = makeFont (10.0f, true).withExtraKerningFactor (0.2f);
        const auto brand = makeFont (10.5f, true).withExtraKerningFactor (0.2f);
        const float fromWidth = textWidth (from, "FROM");
        g.setFont (from);
        g.setColour (Colours::label);
        g.drawText ("FROM", juce::Rectangle<float> (59.0f, 34.0f, fromWidth + 4.0f, 13.0f), juce::Justification::centredLeft, false);
        g.setFont (brand);
        g.setColour (Colours::charcoal);
        g.drawText ("MIWN", juce::Rectangle<float> (59.0f + fromWidth + 6.0f, 34.0f, 80.0f, 13.0f),
                    juce::Justification::centredLeft, false);
    }

    // Rótulos de la fila superior
    g.setColour (Colours::label);
    g.setFont (makeFont (8.5f, true));
    g.drawText ("M O D E", 216, 2, 120, 10, juce::Justification::centredLeft, false);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("GAIN", 654, 12, 36, 34, juce::Justification::centredLeft, false);

    // Tarjetas
    auto card = [&] (juce::Rectangle<float> r)
    {
        g.setColour (Colours::white);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::border.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    };
    card ({ 16.0f, kInstCardY, 818.0f, kInstCardH });      // INSTRUMENT (2.0)
    card ({ 16.0f, kMicCardY, 818.0f, kMicCardH });        // MIC
    card ({ 16.0f, kKeyCardY, 410.0f, kKeyCardH });        // KEY
    card ({ 436.0f, kKeyCardY, 398.0f, kKeyCardH });       // SCALE
    card ({ 16.0f, kScaleBarY, 818.0f, kScaleBarH });      // ESCALA
    card ({ 16.0f, kKeyboardY, 818.0f, kKeyboardH });      // LIVE KEYBOARD

    // Rótulo del GATE (dentro de la tarjeta MIC: se dibuja después de ella)
    g.setColour (Colours::label);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("GATE", 344, 64 + kMicRowShift, 34, 28, juce::Justification::centredLeft, false);

    // Pestañas de acento sobre INSTRUMENT y KEY
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (16.0f, kInstCardY, 28.0f, 3.0f, 1.5f);
    g.fillRoundedRectangle (16.0f, kKeyCardY, 28.0f, 3.0f, 1.5f);
    g.setColour (Colours::label);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("K E Y", 30, (int) kKeyCardY + 6, 120, 18, juce::Justification::centredLeft, false);

    g.drawText ("L I V E   K E Y B O A R D", 30, (int) kKeyboardY + 6, 260, 16, juce::Justification::centredLeft, false);
    g.setFont (makeFont (10.0f, true));
    g.drawText (u8 ("Teclado PC: A W S E D F T G Y H U J K  ·  Z / X octava"), 270, (int) kKeyboardY + 6, 306, 16,
                juce::Justification::centredRight, false);
}

void SmartKeysAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) kWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, kWidth, kHeight);
    audioProcessor.setEditorScale (scale);
    layoutContent();
}

void SmartKeysAudioProcessorEditor::layoutContent()
{
    // Fila superior: logo + título | SMART CHORDS · SINGLE NOTES · COMPLEX CHORDS | GAIN
    modeSwitch.setBounds (212, 12, 422, 34);
    gainSlider.setBounds (692, 15, 142, 28);
    portStatus.setBounds (560, 1, 274, 12);   // "MIDI ● SmartKeys Chords", alineado a la derecha

    // Fila INSTRUMENT: ranura | SHOW PLUGIN GUI | BYPASS | UNLOAD | PRESET
    const int iy = (int) kInstCardY;
    instrumentSlot.setBounds (22, iy + 4, 318, 36);
    showGuiButton.setBounds (346, iy + 8, 150, 28);
    bypassButton.setBounds (500, iy + 8, 84, 28);
    unloadButton.setBounds (588, iy + 8, 84, 28);
    presetBox.setBounds (680, iy + 4, 148, 36);

    // Tira MIC: MIC VOICE | medidor | GATE | Mic: A3 -> Acorde: Am
    micButton.setBounds (22, 64 + kMicRowShift, 150, 28);
    micMeter.setBounds (184, 70 + kMicRowShift, 150, 16);
    gateSlider.setBounds (378, 66 + kMicRowShift, 140, 24);
    voiceReadout.setBounds (534, 62 + kMicRowShift, 292, 32);

    // KEY · SCALE · ESCALA
    keySelector.setBounds (30, (int) kKeyCardY + 28, 382, 132);
    scaleSelector.setBounds (448, (int) kKeyCardY + 6, 374, 158);
    scaleBar.setBounds (24, (int) kScaleBarY + 4, 802, 74);

    // LIVE KEYBOARD | display | voicing | STRUM SPEED + ASC/DESC
    const int y = (int) kKeyboardY;
    piano.setBounds (24, y + 28, 552, 182);
    chordDisplay.setBounds (590, y + 8, 236, 80);
    chordTypeSwitch.setBounds (587, y + 92, 242, 30);
    variationSwitch.setBounds (587, y + 92, 242, 30);
    strumKnob.setBounds (590, y + 130, 46, 46);
    strumTitleLabel.setBounds (642, y + 132, 186, 16);
    strumValueLabel.setBounds (642, y + 148, 186, 26);
    strumDirSwitch.setBounds (587, y + 180, 242, 30);

    presetDialog.setBounds (0, 0, kWidth, kHeight);
}

//==============================================================================
void SmartKeysAudioProcessorEditor::timerCallback()
{
    {
        using PortState = smartkeys::ui::ChordPortStatus::State;
        auto state = PortState::Hidden;
        switch (audioProcessor.getChordPortState())
        {
            case ChordPortState::Off:          state = PortState::Off; break;
            case ChordPortState::Waiting:      state = PortState::Waiting; break;
            case ChordPortState::Active:
                if (audioProcessor.isPianoRollChords())
                {
                    // Con el transporte parado llegan teclas del DAW: se toca con el teclado de FL
                    const auto now = juce::Time::getMillisecondCounterHiRes();
                    const auto live = audioProcessor.getDawLiveNoteCount();
                    if (live != shownDawLiveNotes)
                    {
                        shownDawLiveNotes = live;
                        dawKeyboardHintUntilMs = now + 6000.0;
                    }
                    state = now < dawKeyboardHintUntilMs                ? PortState::PianoRollFlKeyboard
                          : audioProcessor.isChordPortNotReturning()    ? PortState::PianoRollNotReturning
                          : audioProcessor.isHostRecording()            ? PortState::PianoRollRecording
                                                                        : PortState::PianoRoll;
                }
                else
                {
                    state = audioProcessor.isChordPortLooping() ? PortState::Looping
                          : audioProcessor.isHostRecording()     ? PortState::Recording
                                                                 : PortState::Active;
                }
                break;
            case ChordPortState::Unavailable:  state = PortState::Unavailable; break;
            default:                           break;
        }
        portStatus.setState (state, SmartKeysAudioProcessor::getChordPortName());
    }

    const int root  = juce::jlimit (0, 11, (int) apvts.getRawParameterValue (ParamIDs::rootKey)->load());
    const int scale = juce::jlimit (0, smartkeys::kNumScales - 1, (int) apvts.getRawParameterValue (ParamIDs::scale)->load());
    const uint16_t mask = audioProcessor.getCustomMask();

    if (root != shownRoot || scale != shownScale || mask != shownMask)
    {
        shownRoot = root;
        shownScale = scale;
        shownMask = mask;
        displayEngine.setState (root, scale, mask);
        scaleBar.setState (mask, displayEngine.getScaleDisplayName(), displayEngine.isCustomised(), scale);
        piano.setScaleMask (mask);
        shownEvent = ~(uint64_t) 0;   // fuerza refresco del display
    }

    // Selector inferior según el modo: TRIAD/+8VA/7TH (Smart) o variación (Complex)
    const int mode = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ParamIDs::mode)->load());
    const int variation = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ParamIDs::complexVar)->load());
    if (mode != shownMode || variation != shownVariation)
    {
        shownMode = mode;
        shownVariation = variation;
        chordTypeSwitch.setVisible (mode != 2);
        chordTypeSwitch.setEnabled (mode == 0);
        variationSwitch.setVisible (mode == 2);
        shownEvent = ~(uint64_t) 0;
    }

    piano.setActiveNotes (audioProcessor.getActiveNotesLo(), audioProcessor.getActiveNotesHi());

    // Micrófono: medidor y estado en vivo
    const auto& tracker = audioProcessor.getVoiceTracker();
    micMeter.setState (audioProcessor.getMicLevelDb(), apvts.getRawParameterValue (ParamIDs::micGate)->load(),
                       tracker.gateOpen.load(), audioProcessor.isMicSourceAvailable());

    updateVoiceReadout();
    updateStrumLabel();
    updateDisplay();
    updateInstrumentRow();

    // Teclado de la PC: lo que se perdiera entre tecla y tecla se corrige; y si el foco se fue a
    // otra ventana (el KeyUp ya no llega aquí), sus notas se sueltan
    auto& hostedForKeys = audioProcessor.getHostedInstrument();
    const bool keysReachUs = hasKeyboardFocus (true) || hostedForKeys.isWindowActive();
    if (keysReachUs)
        pollComputerKeys();

    if (keyFocusCheckAtMs > 0.0 && juce::Time::getMillisecondCounterHiRes() >= keyFocusCheckAtMs)
    {
        keyFocusCheckAtMs = 0.0;
        if (! keysReachUs)
            releaseComputerKeys();
    }
}

void SmartKeysAudioProcessorEditor::updateStrumLabel()
{
    const double strumMs = audioProcessor.getStrumMs();
    const bool off = strumMs < 0.05;
    const auto text = off ? juce::String ("OFF")
                          : (strumMs < 10.0 ? juce::String (strumMs, 1) : juce::String (juce::roundToInt (strumMs))) + " ms";
    if (strumValueLabel.getText() != text)
    {
        strumValueLabel.setText (text, juce::dontSendNotification);
        strumValueLabel.setColour (juce::Label::textColourId, off ? Colours::disabled : Colours::accent);
    }
}

void SmartKeysAudioProcessorEditor::updateVoiceReadout()
{
    using smartkeys::MicDeviceState;

    const bool micOn = apvts.getRawParameterValue (ParamIDs::micOn)->load() > 0.5f;
    const int source = audioProcessor.getVoiceSource();
    const auto micState = audioProcessor.getSystemMicState();
    const bool inputSilent = audioProcessor.getMicLevelDb() <= -100.0f + 0.01f;

    auto setTip = [this] (const juce::String& tip)
    {
        if (voiceReadout.getTooltip() != tip)
            voiceReadout.setTooltip (tip);
    };

    if (! micOn)
    {
        setTip ({});
        voiceReadout.showNothing();
        return;
    }

    // Avisos sólo si hace falta el micrófono del sistema y no se puede usar
    if (source != VoiceSource::SystemMic && inputSilent)
    {
        const juce::String host (juce::PluginHostType().getHostDescription());
        const auto hostName = host == "Unknown" ? u8 ("el programa") : host;

        auto warn = [&] (const juce::String& message, const juce::String& tip, bool isWarning)
        {
            setTip (tip);
            voiceReadout.showMessage (message, isWarning);
        };

        switch (micState)
        {
            case MicDeviceState::WaitingPermission:
                warn (u8 ("Permite el micrófono en el aviso de macOS"),
                      u8 ("macOS pregunta si ") + hostName + u8 (" puede usar el micrófono: pulsa Permitir."), false);
                return;
            case MicDeviceState::PermissionDenied:
                warn (u8 ("Activa ") + hostName + u8 (" en Privacidad > Micrófono"),
                      u8 ("macOS no deja que ") + hostName + u8 (" use el micrófono. Abre Ajustes del Sistema > "
                      "Privacidad y seguridad > Micrófono, activa ") + hostName + u8 (" y vuelve a abrirlo."), true);
                return;
            case MicDeviceState::HostNotAllowed:
                warn (u8 ("Este programa no permite usar el micrófono"),
                      u8 ("La aplicación que carga SmartKeys no declara el uso del micrófono, así que macOS no se lo "
                          "permitiría. Usa la entrada sidechain del plugin."), true);
                return;
            case MicDeviceState::NoDevice:
                warn (u8 ("No hay ninguna entrada de audio en macOS"),
                      u8 ("Conecta un micrófono o elige uno en Ajustes del Sistema > Sonido > Entrada."), true);
                return;
            case MicDeviceState::BluetoothOnly:
                warn (u8 ("Micro Bluetooth: elige otra entrada en Sonido"),
                      u8 ("Abrir el micrófono de unos auriculares Bluetooth (AirPods) les cambia el sonido a modo "
                          "llamada. Elige el micrófono de la Mac o tu interfaz en Ajustes del Sistema > Sonido > Entrada."), true);
                return;
            case MicDeviceState::Error:
                warn (u8 ("No se pudo abrir el micrófono (reintentando)"),
                      u8 ("La entrada de audio de macOS no se pudo abrir; SmartKeys lo vuelve a intentar cada pocos segundos."), true);
                return;
            case MicDeviceState::Unsupported:
               #if JUCE_WINDOWS
                warn (u8 ("Mic: sin señal · envía la voz por el sidechain"),
                      u8 ("En Windows la voz entra por la entrada sidechain del plugin."), false);
                return;
               #else
                break;
               #endif
            case MicDeviceState::Off:
            case MicDeviceState::Running:
                break;
        }
    }

    // Ayuda al pasar el ratón: de dónde sale la voz
    if (source == VoiceSource::SystemMic)
    {
        auto tip = u8 ("Micrófono: ") + audioProcessor.getSystemMicDeviceName() + "  ("
                   + juce::String (audioProcessor.getSystemMicSampleRate() / 1000.0, 1) + " kHz, canal 1)";
        if (audioProcessor.isSystemMicReplacingBluetooth())
            tip << u8 ("\nLa entrada de macOS es Bluetooth: se usa el micrófono de la Mac para no cambiar el sonido de los auriculares.");
        setTip (tip);
    }
    else
    {
        setTip (source == VoiceSource::PluginInput && ! inputSilent
                    ? u8 ("La voz llega por la entrada del plugin (la manda el DAW).") : juce::String());
    }

    // "Mic: A3 -> Acorde: Am": la nota corregida que dispara y lo que suena
    const auto& tracker = audioProcessor.getVoiceTracker();
    const int mode = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ParamIDs::mode)->load());

    if (audioProcessor.isVoiceChordSounding())
    {
        const auto ev = smartkeys::MidiOutputRouter::decodeLastEvent (audioProcessor.getLastVoiceEventPacked());
        if (ev.valid && ! ev.notes.empty())
        {
            const auto noteName = displayEngine.getNoteName (ev.triggerNote);
            if (ev.mode == 1 || ev.notes.size() == 1)
                voiceReadout.showVoice (noteName, "Nota", displayEngine.getNoteName (ev.notes.front()));
            else
                voiceReadout.showVoice (noteName, "Acorde", displayEngine.getChordName (ev.notes));
            return;
        }
    }

    const float detected = tracker.gateOpen.load() ? tracker.detectedMidiNote.load() : -1.0f;
    if (detected >= 0.0f)
        voiceReadout.showVoice (displayEngine.getNoteName ((int) std::lround (detected)), mode == 1 ? "Nota" : "Acorde", {});
    else
        voiceReadout.showIdle();
}

void SmartKeysAudioProcessorEditor::updateDisplay()
{
    const uint64_t packed = audioProcessor.getLastEventPacked();
    const uint64_t lo = audioProcessor.getActiveNotesLo();
    const uint64_t hi = audioProcessor.getActiveNotesHi();
    const float detected = audioProcessor.getVoiceTracker().detectedMidiNote.load();
    const int voiceNote = detected >= 0.0f ? (int) std::lround (detected) : -1;

    if (packed == shownEvent && lo == shownLo && hi == shownHi && voiceNote == shownVoiceNote)
        return;

    shownEvent = packed;
    shownLo = lo;
    shownHi = hi;
    shownVoiceNote = voiceNote;

    const bool sounding = (lo | hi) != 0;
    const auto ev = smartkeys::MidiOutputRouter::decodeLastEvent (packed);
    const int mode = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ParamIDs::mode)->load());
    const auto variationName = smartkeys::ScaleChordEngine::getComplexVariationNames()
                                   [juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ParamIDs::complexVar)->load())];

    if (! ev.valid)
    {
        juce::String idle;
        if (mode == 2)      idle = "Complex Chords\n" + variationName;
        else if (mode == 1) idle = u8 ("Single Notes · toca o canta");
        else                idle = u8 ("Smart Chords · toca o canta");
        chordDisplay.setText ("--", idle, false);
        return;
    }

    const auto name = displayEngine.getChordName (ev.notes);
    juce::String notes;
    for (int n : ev.notes)
        notes << (notes.isEmpty() ? "" : " ") << displayEngine.getNoteName (n);

    juce::String detail;
    if (ev.fromVoice)
    {
        const int shownInput = voiceNote >= 0 ? voiceNote : ev.triggerNote;
        detail = "Voice Input: " + displayEngine.getNoteName (shownInput) + " -> " + name;
    }
    else
    {
        detail = "Key " + displayEngine.getNoteName (ev.triggerNote) + "  ->  " + notes;
    }

    // Acorde complejo: primera línea con la variación usada
    if (ev.mode == 2)
        detail = variationName + "\n" + detail;

    chordDisplay.setText (name, detail, sounding);
}

//==============================================================================
// Clic en "MIDI ● SmartKeys Chords": dónde se graban los acordes (o apagarlo)
void SmartKeysAudioProcessorEditor::showChordPortMenu()
{
    if (! audioProcessor.canUsePianoRollChords())
    {
        audioProcessor.setChordPortEnabled (! audioProcessor.isChordPortEnabled());   // la app: encender / apagar
        return;
    }

    const bool on = audioProcessor.isChordPortEnabled();
    const bool pianoRoll = on && audioProcessor.isPianoRollChords();

    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ("Acordes por \"") + SmartKeysAudioProcessor::getChordPortName() + "\"");
    menu.addItem (1, u8 ("Acordes al Piano Roll de SmartKeys (toca en SmartKeys)"), true, pianoRoll);
    menu.addItem (2, u8 ("Acordes a otro instrumento (FLEX, Serum…)"), true, on && ! pianoRoll);
    menu.addSeparator();
    menu.addItem (3, u8 ("Apagado"), true, ! on);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&portStatus),
                        [safe = juce::Component::SafePointer<SmartKeysAudioProcessorEditor> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& p = safe->audioProcessor;
                            if (result == 1)       p.setPianoRollChords (true);
                            else if (result == 2) { p.setPianoRollChords (false); p.setChordPortEnabled (true); }
                            else                   p.setChordPortEnabled (false);
                            safe->timerCallback();
                        });
}

//==============================================================================
// 2.0: fila INSTRUMENT (la ranura, su ventana, BYPASS, UNLOAD y los presets)
void SmartKeysAudioProcessorEditor::updateInstrumentRow (bool force)
{
    using Look = smartkeys::ui::InstrumentSlot::Look;
    using Status = smartkeys::HostedInstrument::Status;

    auto& hosted = audioProcessor.getHostedInstrument();
    auto& catalog = hosted.getCatalog();
    const auto hostedChange = hosted.getChangeCounter();
    const auto catalogChange = catalog.getChangeCounter();
    const auto presetName = audioProcessor.getPresetName();
    const auto failure = hosted.getRecentFailure();
    const bool windowOpen = hosted.isWindowOpen();
    const bool invalidAudio = hosted.producedInvalidAudio();

    if (! force && hostedChange == shownHostedChange && catalogChange == shownCatalogChange && presetName == shownPresetName
          && failure == shownFailure && windowOpen == shownWindowOpen && invalidAudio == shownInvalidAudio)
        return;

    shownHostedChange = hostedChange;
    shownCatalogChange = catalogChange;
    shownPresetName = presetName;
    shownFailure = failure;
    shownWindowOpen = windowOpen;
    shownInvalidAudio = invalidAudio;

    const auto status = hosted.getStatus();
    const auto d = hosted.getDescription();
    juce::String kind = smartkeys::PluginCatalog::getFormatDisplayName (d);
    if (d.manufacturerName.isNotEmpty())
        kind << u8 ("  ·  ") << d.manufacturerName;

    juce::String tip;
    switch (status)
    {
        case Status::Empty:
            instrumentSlot.setState (Look::Empty, {}, "+ LOAD PLUGIN", u8 ("Elige el instrumento que tocará los acordes"));
            tip = u8 ("Carga un instrumento VST3 o Audio Unit (Serum, Kontakt, Analog Lab…): SmartKeys le toca los acordes "
                      "completos, con el STRUM, y su sonido sale por SmartKeys. SmartKeys ya no tiene piano propio.");
            break;
        case Status::Loading:
            instrumentSlot.setState (Look::Busy, {}, u8 ("Cargando ") + d.name + u8 ("…"), kind);
            break;
        case Status::Loaded:
        {
            auto sub = hosted.isBypassed() ? u8 ("BYPASS: en silencio (los acordes siguen saliendo por MIDI)") : kind;
            if (invalidAudio)
                sub = u8 ("Devolvió audio no válido: SmartKeys silenció ese momento");
            if (failure.isNotEmpty())
                sub = u8 ("No se pudo cargar ") + failure;
            instrumentSlot.setState (hosted.isBypassed() ? Look::Bypassed : Look::Loaded, "Loaded:", d.name, sub);
            tip = d.name + "  (" + kind + ")\n" + u8 ("Clic: cambiar de instrumento. SHOW PLUGIN GUI abre su ventana.");
            break;
        }
        case Status::Missing:
            instrumentSlot.setState (Look::Problem, {}, d.name, u8 ("No está instalado en esta computadora (se conserva en el proyecto)"));
            tip = u8 ("El proyecto usa ") + d.name + u8 (" (") + kind + u8 ("), que no está en esta computadora. Se guarda tal cual: "
                  "en la computadora que lo tenga sonará con su sonido.");
            break;
        case Status::Failed:
            instrumentSlot.setState (Look::Problem, {}, d.name, u8 ("No se pudo cargar: ") + hosted.getErrorText());
            break;
        case Status::Crashed:
            instrumentSlot.setState (Look::Problem, {}, d.name, u8 ("Se cerró el programa al cargarlo: clic para reintentar"));
            tip = u8 ("La última vez el programa se cerró mientras se cargaba este instrumento; por eso no se volvió a abrir solo. "
                      "Clic > Reintentar para cargarlo (con su sonido guardado).");
            break;
    }

    if (catalog.isScanning())
    {
        const auto name = catalog.getScanningName();
        instrumentSlot.setState (status == Status::Empty ? Look::Busy : (status == Status::Loaded ? Look::Loaded : Look::Problem),
                                 status == Status::Loaded ? juce::String ("Loaded:") : juce::String(),
                                 status == Status::Empty ? u8 ("Buscando instrumentos…") : d.name,
                                 u8 ("Buscando VST3 ") + juce::String (juce::roundToInt (catalog.getScanProgress() * 100.0f)) + "%"
                                     + (name.isNotEmpty() ? u8 ("  ·  ") + juce::File::createFileWithoutCheckingPath (name).getFileNameWithoutExtension()
                                                          : juce::String()));
    }

    if (instrumentSlot.getTooltip() != tip)
        instrumentSlot.setTooltip (tip);

    const bool loaded = status == Status::Loaded;
    showGuiButton.setEnabled (loaded);
    showGuiButton.setToggleState (loaded && windowOpen, juce::dontSendNotification);
    showGuiButton.setButtonText (loaded && windowOpen ? "HIDE PLUGIN GUI" : "SHOW PLUGIN GUI");
    bypassButton.setEnabled (loaded);
    bypassButton.setToggleState (loaded && hosted.isBypassed(), juce::dontSendNotification);
    unloadButton.setEnabled (status != Status::Empty);
    presetBox.setName (presetName);
}

// Clic en la ranura: los instrumentos instalados (Audio Unit al instante; VST3 de la búsqueda)
void SmartKeysAudioProcessorEditor::showInstrumentMenu()
{
    using Status = smartkeys::HostedInstrument::Status;
    auto& hosted = audioProcessor.getHostedInstrument();
    auto& catalog = hosted.getCatalog();

   #if ! JUCE_MAC
    // Windows / Linux: sólo hay VST3. La primera vez se buscan solos.
    if (catalog.hasVst3Format() && ! catalog.hasEverScannedVst3() && ! catalog.isScanning())
        catalog.startVst3Scan (false);
   #endif

    const auto instruments = catalog.getInstruments();
    const auto status = hosted.getStatus();
    const auto current = hosted.getDescription();

    juce::PopupMenu menu;
    auto addSection = [&] (const juce::String& format, const juce::String& header)
    {
        juce::Array<int> indices;
        for (int i = 0; i < instruments.size(); ++i)
            if (instruments.getReference (i).pluginFormatName == format)
                indices.add (i);
        if (indices.isEmpty())
            return false;

        menu.addSectionHeader (header);
        const bool grouped = indices.size() > 10;   // muchos: un submenú por fabricante
        juce::PopupMenu group;
        juce::String maker;
        bool groupTicked = false;

        auto flushGroup = [&]
        {
            if (group.getNumItems() > 0)
                menu.addSubMenu (maker.isNotEmpty() ? maker : u8 ("Otros"), group, true, nullptr, groupTicked);
            group = {};
            groupTicked = false;
        };

        for (int i : indices)
        {
            const auto& d = instruments.getReference (i);
            const bool ticked = status != Status::Empty && d.isDuplicateOf (current);
            if (grouped)
            {
                if (d.manufacturerName != maker)
                {
                    flushGroup();
                    maker = d.manufacturerName;
                }
                group.addItem (i + 1, d.name, true, ticked);
                groupTicked = groupTicked || ticked;
            }
            else
            {
                menu.addItem (i + 1, d.manufacturerName.isNotEmpty() ? d.name + u8 ("   ·   ") + d.manufacturerName : d.name, true, ticked);
            }
        }
        flushGroup();
        return true;
    };

   #if JUCE_MAC
    if (! addSection ("AudioUnit", u8 ("Instrumentos Audio Unit")))
    {
        menu.addSectionHeader (u8 ("Instrumentos Audio Unit"));
        menu.addItem (kMenuInfo, u8 ("No hay ninguno instalado"), false, false);
    }
   #endif

    if (catalog.hasVst3Format())
    {
        if (! addSection ("VST3", u8 ("Instrumentos VST3")))
        {
            menu.addSectionHeader (u8 ("Instrumentos VST3"));
            menu.addItem (kMenuInfo, catalog.isScanning()             ? u8 ("Buscando…")
                                     : catalog.hasEverScannedVst3()   ? u8 ("No se encontró ninguno")
                                                                      : u8 ("Pulsa \"Buscar instrumentos VST3\""), false, false);
        }

        menu.addSeparator();
        if (catalog.isScanning())
        {
            menu.addItem (kMenuScanNew, u8 ("Buscando instrumentos VST3… ")
                                            + juce::String (juce::roundToInt (catalog.getScanProgress() * 100.0f)) + "%", false, false);
        }
        else
        {
            menu.addItem (kMenuScanNew, u8 ("Buscar instrumentos VST3 nuevos"));
            menu.addItem (kMenuScanAll, u8 ("Volver a buscar todos los VST3"));
        }
    }

    if (status == Status::Missing || status == Status::Failed || status == Status::Crashed)
    {
        menu.addSeparator();
        menu.addItem (kMenuRetry, u8 ("Reintentar cargar \"") + current.name + u8 ("\""));
    }
    if (status != Status::Empty)
    {
        menu.addSeparator();
        menu.addItem (kMenuUnload, u8 ("Quitar el instrumento (UNLOAD)"));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentSlot)
                                                  .withMinimumWidth (instrumentSlot.getWidth()),
                        [safe = juce::Component::SafePointer<SmartKeysAudioProcessorEditor> (this), instruments] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& h = safe->audioProcessor.getHostedInstrument();
                            if (result == kMenuScanNew)       h.getCatalog().startVst3Scan (false);
                            else if (result == kMenuScanAll)  h.getCatalog().startVst3Scan (true);
                            else if (result == kMenuRetry)    h.retry();
                            else if (result == kMenuUnload)   h.unload();
                            else if (juce::isPositiveAndBelow (result - 1, instruments.size()))
                                h.load (instruments.getReference (result - 1));
                            safe->updateInstrumentRow (true);
                        });
}

// Clic en PRESET: los de fábrica, los del usuario, guardar, borrar y la carpeta
void SmartKeysAudioProcessorEditor::showPresetMenu()
{
    namespace presets = smartkeys::presets;
    const auto items = presets::list();
    const auto current = audioProcessor.getPresetName();

    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ("Presets de fábrica"));
    bool userHeader = false;
    int currentUser = -1;
    for (int i = 0; i < items.size(); ++i)
    {
        const auto& item = items.getReference (i);
        if (! item.isFactory() && ! userHeader)
        {
            menu.addSectionHeader (u8 ("Mis presets"));
            userHeader = true;
        }
        menu.addItem (i + 1, item.name, true, item.name == current);
        if (! item.isFactory() && item.name == current)
            currentUser = i;
    }
    if (! userHeader)
    {
        menu.addSectionHeader (u8 ("Mis presets"));
        menu.addItem (kMenuInfo, u8 ("Todavía ninguno"), false, false);
    }

    menu.addSeparator();
    menu.addItem (kMenuSavePreset, u8 ("Guardar preset…"));
    if (currentUser >= 0)
        menu.addItem (kMenuDeletePreset, u8 ("Borrar \"") + current + u8 ("\"…"));
    menu.addItem (kMenuPresetFolder, u8 ("Abrir la carpeta de presets"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBox),
                        [safe = juce::Component::SafePointer<SmartKeysAudioProcessorEditor> (this), items, currentUser] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& editor = *safe;
                            if (result == kMenuSavePreset)
                            {
                                auto& hosted = editor.audioProcessor.getHostedInstrument();
                                const bool loaded = hosted.getStatus() == smartkeys::HostedInstrument::Status::Loaded;
                                const auto name = currentUser >= 0 ? items.getReference (currentUser).name : u8 ("Mi preset");
                                editor.presetDialog.showSave (name, loaded ? hosted.getDescription().name : juce::String());
                            }
                            else if (result == kMenuDeletePreset && currentUser >= 0)
                            {
                                const auto item = items.getReference (currentUser);
                                editor.presetDialog.onDelete = [safe, item]
                                {
                                    if (safe == nullptr)
                                        return;
                                    if (presets::remove (item))
                                        safe->audioProcessor.setPresetName ({});
                                    safe->updateInstrumentRow (true);
                                };
                                editor.presetDialog.showDelete (item.name);
                            }
                            else if (result == kMenuPresetFolder)
                            {
                                const auto folder = presets::getUserFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                            }
                            else if (juce::isPositiveAndBelow (result - 1, items.size()))
                            {
                                const auto& item = items.getReference (result - 1);
                                if (auto xml = presets::load (item))
                                    editor.audioProcessor.applyPreset (*xml, item.name);
                            }
                            editor.updateInstrumentRow (true);
                        });
}

//==============================================================================
bool SmartKeysAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (presetDialog.isVisible())
        return false;   // se está escribiendo el nombre de un preset

    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (key.getModifiers().isCommandDown())
        return false;

    if (c == 'z') { computerOctave = juce::jmax (0, computerOctave - 1); return true; }
    if (c == 'x') { computerOctave = juce::jmin (8, computerOctave + 1); return true; }

    for (const char* k = kKeyMap; *k != 0; ++k)
        if (c == (juce::juce_wchar) *k)
            return true;

    return false;
}

bool SmartKeysAudioProcessorEditor::keyStateChanged (bool)
{
    return pollComputerKeys();
}

bool SmartKeysAudioProcessorEditor::pollComputerKeys()
{
    bool handled = false;
    const bool command = juce::ModifierKeys::currentModifiers.isCommandDown() || presetDialog.isVisible();

    for (int i = 0; i < 13; ++i)
    {
        const bool down = ! command && juce::KeyPress::isKeyCurrentlyDown ((int) kKeyMap[i]);
        if (down == computerKeysDown[(size_t) i])
            continue;

        computerKeysDown[(size_t) i] = down;
        handled = true;

        if (down)
        {
            const int note = juce::jlimit (0, 127, (computerOctave + 1) * 12 + i);
            computerKeyNotes[(size_t) i] = note;
            audioProcessor.pushScreenNote (note, 100, true);
        }
        else if (computerKeyNotes[(size_t) i] >= 0)
        {
            audioProcessor.pushScreenNote (computerKeyNotes[(size_t) i], 0, false);
            computerKeyNotes[(size_t) i] = -1;
        }
    }

    return handled;
}

void SmartKeysAudioProcessorEditor::releaseComputerKeys()
{
    // Se suelta lo que suena, pero la tecla sigue contando como pulsada: si de verdad lo
    // está (o el sistema todavía lo cree, porque la soltó en otra ventana), no vuelve a
    // disparar un acorde por sí sola; tocará otra vez cuando se suelte y se vuelva a pulsar
    for (size_t i = 0; i < computerKeysDown.size(); ++i)
    {
        if (computerKeyNotes[i] >= 0)
            audioProcessor.pushScreenNote (computerKeyNotes[i], 0, false);
        computerKeyNotes[i] = -1;
    }
}

void SmartKeysAudioProcessorEditor::releaseUiNotes()
{
    piano.releaseMouseNote();
    releaseComputerKeys();
    audioProcessor.releaseScreenNotes();   // y lo que quedara de la pantalla en el hilo de audio
}
