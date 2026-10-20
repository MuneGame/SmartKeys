/*
  ==============================================================================

    PluginEditor.cpp
    SmartKeys (MIWN) — Interfaz gráfica vectorial (850 x 606 px, redimensionable).

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

KeySelector::KeySelector (juce::RangedAudioParameter& rootParam)
    : attachment (rootParam, [this] (float v) { selected = juce::jlimit (0, 11, (int) std::lround (v)); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> KeySelector::getKeyBounds (int pc) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float gap = 8.0f;
    const float whiteW = (r.getWidth() - 6.0f * gap) / 7.0f;
    const float whiteH = r.getHeight() * 0.47f;
    const float blackW = whiteW * 0.86f;
    const float blackH = r.getHeight() * 0.40f;

    if (! isBlackPc (pc))
    {
        const int wi = kWhiteIndex[pc];
        return { r.getX() + (float) wi * (whiteW + gap), r.getBottom() - whiteH, whiteW, whiteH };
    }

    const int left = kBlackLeftWhite[pc];
    const float centre = r.getX() + (float) left * (whiteW + gap) + whiteW + gap * 0.5f;
    return { centre - blackW * 0.5f, r.getY() + 2.0f, blackW, blackH };
}

int KeySelector::hitTestKey (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc)
        if (getKeyBounds (pc).contains (p))
            return pc;
    return -1;
}

void KeySelector::paint (juce::Graphics& g)
{
    for (int pc = 0; pc < 12; ++pc)
    {
        const auto r = getKeyBounds (pc);
        const bool black = isBlackPc (pc);
        const bool sel = (pc == selected);
        const bool hov = (pc == hovered);

        if (sel)
        {
            drawGlowBox (g, r, 3.0f, Colours::accent, true);
            g.setColour (Colours::white);
        }
        else if (black)
        {
            g.setColour (hov ? Colours::charcoal.brighter (0.25f) : Colours::charcoal);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::disabled.darker (0.3f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (Colours::white);
        }
        else
        {
            g.setColour (hov ? Colours::white : Colours::softPanel);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::border);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (Colours::text);
        }

        drawTwoLineLabel (g, keyLabel (pc), r, black ? 15.0f : 19.0f);
    }
}

void KeySelector::mouseDown (const juce::MouseEvent& e)
{
    const int pc = hitTestKey (e.position);
    if (pc >= 0)
        attachment.setValueAsCompleteGesture ((float) pc);
}

void KeySelector::mouseMove (const juce::MouseEvent& e)
{
    const int pc = hitTestKey (e.position);
    if (pc != hovered) { hovered = pc; repaint(); }
}

void KeySelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
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

int PianoVisualizer::noteAt (juce::Point<float> p, int& velocity) const
{
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (isBlack (n) && getKeyRect (n).contains (p))
        {
            const auto r = getKeyRect (n);
            velocity = juce::jlimit (30, 127, (int) (40.0f + 87.0f * (p.y - r.getY()) / r.getHeight()));
            return n;
        }
    }
    for (int n = kLowestNote; n <= kHighestNote; ++n)
    {
        if (! isBlack (n) && getKeyRect (n).contains (p))
        {
            const auto r = getKeyRect (n);
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
    int vel = 100;
    const int n = noteAt (e.position, vel);
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
    const auto clickOff = u8 ("\n(Clic aquí: apagar el teclado)");
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

    const bool problem = state == State::Unavailable || state == State::Looping;
    juce::String suffix;
    if (state == State::Unavailable)  suffix = u8 (" · en uso");
    if (state == State::Looping)      suffix = u8 (" · eco");
    if (state == State::Off)          suffix = u8 (" · OFF");
    if (state == State::Recording)    suffix = u8 (" · REC");
    const bool on = state == State::Active || state == State::Recording;

    const float nameW = textWidth (main, name + suffix);
    const float tagW = textWidth (small, "MIDI");
    float x = r.getRight() - nameW;

    g.setFont (main);
    auto textColour = problem ? Colours::warning
                    : state == State::Recording ? Colours::accent
                    : (on ? Colours::charcoal : Colours::label);
    if (isMouseOver())
        textColour = problem || state == State::Recording ? textColour.darker (0.3f) : Colours::accent;
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

} // namespace smartkeys::ui

//==============================================================================
// EDITOR
//==============================================================================
using namespace smartkeys::ui;

namespace
{
    // Coordenadas del contenido (850 x 606): tarjetas y filas
    constexpr float kMicCardY   = 56.0f,  kMicCardH   = 44.0f;
    constexpr float kKeyCardY   = 108.0f, kKeyCardH   = 170.0f;
    constexpr float kScaleBarY  = 286.0f, kScaleBarH  = 84.0f;
    constexpr float kKeyboardY  = 378.0f, kKeyboardH  = 218.0f;
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
    gainSlider.setTooltip (u8 ("Volumen del piano interno de SmartKeys. Los acordes salen siempre por la salida MIDI "
                               "del plugin, en el canal 1 (en FL Studio: engranaje > MIDI > Output port)."));
    gateSlider.setTooltip (u8 ("Umbral del micrófono: por debajo, la voz no dispara notas"));

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
                              "elegido en tiempo real (suena el piano y sale por MIDI).\n"
                              "SmartKeys lee la entrada de audio activa de macOS (Ajustes del Sistema > Sonido > Entrada); "
                              "si el DAW le manda audio por su entrada (sidechain), usa ese."));
   #else
    micButton.setTooltip (u8 ("Canta o tararea: tu voz, corregida a KEY y SCALE, dispara la nota o el acorde del modo "
                              "elegido en tiempo real (suena el piano y sale por MIDI).\n"
                              "En Windows la voz entra por la entrada sidechain del plugin."));
   #endif
    content.addAndMakeVisible (micButton);

    content.addAndMakeVisible (micMeter);
    content.addAndMakeVisible (voiceReadout);

    //--------------------------------------------------------------------------
    // KEY · SCALE · ESCALA
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
    portStatus.onClick = [this] { audioProcessor.setChordPortEnabled (! audioProcessor.isChordPortEnabled()); };
    audioProcessor.markInUse();

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
    for (size_t i = 0; i < computerKeysDown.size(); ++i)
        if (computerKeysDown[i] && computerKeyNotes[i] >= 0)
            audioProcessor.pushScreenNote (computerKeyNotes[i], 0, false);

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
    card ({ 16.0f, kMicCardY, 818.0f, kMicCardH });        // MIC
    card ({ 16.0f, kKeyCardY, 410.0f, kKeyCardH });        // KEY
    card ({ 436.0f, kKeyCardY, 398.0f, kKeyCardH });       // SCALE
    card ({ 16.0f, kScaleBarY, 818.0f, kScaleBarH });      // ESCALA
    card ({ 16.0f, kKeyboardY, 818.0f, kKeyboardH });      // LIVE KEYBOARD

    // Rótulo del GATE (dentro de la tarjeta MIC: se dibuja después de ella)
    g.setColour (Colours::label);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("GATE", 344, 64, 34, 28, juce::Justification::centredLeft, false);

    // Pestaña de acento sobre KEY
    g.setColour (Colours::accent);
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

    // Tira MIC: MIC VOICE | medidor | GATE | Mic: A3 -> Acorde: Am
    micButton.setBounds (22, 64, 150, 28);
    micMeter.setBounds (184, 70, 150, 16);
    gateSlider.setBounds (378, 66, 140, 24);
    voiceReadout.setBounds (534, 62, 292, 32);

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
            case ChordPortState::Active:       state = audioProcessor.isChordPortLooping() ? PortState::Looping
                                                      : audioProcessor.isHostRecording()     ? PortState::Recording
                                                                                             : PortState::Active; break;
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
bool SmartKeysAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
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
    bool handled = false;
    const bool command = juce::ModifierKeys::currentModifiers.isCommandDown();

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
