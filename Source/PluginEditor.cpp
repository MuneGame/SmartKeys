/*
  ==============================================================================

    PluginEditor.cpp
    SmartKeys — Interfaz gráfica vectorial (850 x 686 px, redimensionable).

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
// DragChordsButton
//==============================================================================
DragChordsButton::DragChordsButton (SmartKeysAudioProcessor& p, std::function<juce::String()> baseNameProvider)
    : processor (p), makeBaseName (std::move (baseNameProvider))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragChordsButton::setCount (int numChords, bool isFull, bool isCapturing)
{
    bool needsRepaint = (numChords != count || isFull != full || isCapturing != capturing);
    capturing = isCapturing;

    if (flashText.isNotEmpty() && juce::Time::getMillisecondCounter() >= flashUntilMs)
    {
        flashText.clear();
        needsRepaint = true;
    }

    count = numChords;
    full = isFull;
    if (needsRepaint)
        repaint();
}

void DragChordsButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const bool enabled = count > 0;

    if (enabled)
    {
        const auto fill = pressed ? Colours::accent.darker (0.15f) : (hover ? Colours::accent.brighter (0.08f) : Colours::accent);
        drawGlowBox (g, r, 4.0f, fill, hover);
    }
    else
    {
        g.setColour (Colours::softPanel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Colours::border);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    // Icono: tres barras (las notas de un acorde)
    auto content = r.reduced (8.0f, 0.0f);
    auto icon = content.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 13.0f);
    g.setColour (enabled ? Colours::white : Colours::disabled);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (icon.getX() + (float) (i % 2) * 3.0f, icon.getY() + (float) i * 4.5f,
                                icon.getWidth() - 3.0f, 3.0f, 1.0f);

    juce::String text;
    if (flashText.isNotEmpty() && juce::Time::getMillisecondCounter() < flashUntilMs)
        text = flashText;
    else if (capturing)
        text = u8 ("● GRABANDO ACORDES (") + juce::String (count) + ")";
    else if (count == 0)
        text = "DRAG CHORDS TO PIANO ROLL";
    else
        text = "DRAG CHORDS TO PIANO ROLL (" + juce::String (count) + (full ? "+)" : ")");

    g.setFont (makeFont (11.5f, true));
    g.drawFittedText (text, content.toNearestInt().withTrimmedLeft (4), juce::Justification::centred, 1, 0.65f);
}

void DragChordsButton::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void DragChordsButton::mouseExit (const juce::MouseEvent&)  { hover = false; repaint(); }

void DragChordsButton::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    dragStarted = false;
    repaint();
}

void DragChordsButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || count == 0 || e.getDistanceFromDragStart() < 6)
        return;

    dragStarted = true;
    pressed = false;
    repaint();

    const auto file = exportNow();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void DragChordsButton::mouseUp (const juce::MouseEvent& e)
{
    const bool wasClick = pressed && ! dragStarted && getLocalBounds().contains (e.getPosition());
    pressed = false;

    if (wasClick && count > 0)
    {
        const auto file = exportNow();
        if (file.existsAsFile())
        {
            file.revealToUser();
            flashText = "GUARDADO: " + file.getFileName();
        }
        else
        {
            flashText = "NO SE PUDO GUARDAR EL .MID";
        }
        flashUntilMs = juce::Time::getMillisecondCounter() + 2500;
    }
    repaint();
}

juce::File DragChordsButton::exportNow()
{
    return processor.exportProgression (makeBaseName != nullptr ? makeBaseName() : juce::String ("SmartKeys Chords"));
}


} // namespace smartkeys::ui

//==============================================================================
// EDITOR
//==============================================================================
using namespace smartkeys::ui;

SmartKeysAudioProcessorEditor::SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      apvts (p.getValueTreeState()),
      modeSwitch (*apvts.getParameter (ParamIDs::mode), { "SMART CHORDS", "SINGLE NOTES", "COMPLEX CHORDS" }),
      outputSwitch (*apvts.getParameter (ParamIDs::outputMode), { "PIANO + MIDI", "MIDI OUT" }),
      keySelector (*apvts.getParameter (ParamIDs::rootKey)),
      scaleSelector (*apvts.getParameter (ParamIDs::scale)),
      scaleBar (p, *apvts.getParameter (ParamIDs::scale)),
      lane (p),
      palette (p, lane),
      piano (p),
      chordTypeSwitch (*apvts.getParameter (ParamIDs::chordExt), { "TRIAD", "+8VA", "7TH" }),
      variationSwitch (*apvts.getParameter (ParamIDs::complexVar), { "LUSH 7/9", "NEO-SOUL", "CINEMATIC" }),
      strumDirSwitch (*apvts.getParameter (ParamIDs::strumDir), { u8 ("ASC ▲"), u8 ("DESC ▼") }),
      dragButton (p, [this] { return makeExportBaseName(); })
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };

    auto setupToggle = [this] (juce::TextButton& b, const juce::String& prefix, bool showState, float fontSize)
    {
        b.setClickingTogglesState (true);
        b.getProperties().set (fontSizeProperty, fontSize);
        b.onStateChange = [&b, prefix, showState]
        {
            const auto text = showState ? prefix + (b.getToggleState() ? "ON" : "OFF") : prefix;
            if (b.getButtonText() != text)
                b.setButtonText (text);
        };
        content.addAndMakeVisible (b);
    };

    //--------------------------------------------------------------------------
    // Fila 1: modo + OUTPUT MODE (la salida MIDI de los acordes está siempre activa)
    modeSwitch.setButtonFontSize (12.5f);
    modeSwitch.setTooltips ({ u8 ("1 tecla = acorde diatónico (tríada / +8va / 7ª)"),
                              u8 ("1 tecla = 1 nota corregida a la escala"),
                              u8 ("1 tecla = acorde de 5 a 7 notas a dos manos (R&B / Neo-Soul)") });
    content.addAndMakeVisible (modeSwitch);

    outputSwitch.setButtonFontSize (11.5f);
    outputSwitch.setTooltips ({ u8 ("PIANO + MIDI: suena el piano de SmartKeys y los acordes completos salen por MIDI hacia otros plugins"),
                                u8 ("MIDI OUT: sólo salen los acordes por MIDI (el piano interno se silencia) para tocar otro plugin") });
    content.addAndMakeVisible (outputSwitch);

    //--------------------------------------------------------------------------
    // Fila 2: afinación, canal MIDI, microafinación, pánico, ganancia
    for (auto* s : { &gainSlider, &gateSlider, &tuningSlider })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        s->setColour (juce::Slider::textBoxTextColourId, Colours::text);
        content.addAndMakeVisible (s);
    }
    gainAttach   = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::masterGain), gainSlider);
    gateAttach   = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::micGate), gateSlider);
    tuningAttach = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::refPitch), tuningSlider);

    tune440.onClick = [this] { setRefPitch (440.0f); };
    tune442.onClick = [this] { setRefPitch (442.0f); };
    content.addAndMakeVisible (tune440);
    content.addAndMakeVisible (tune442);

    for (int ch = 1; ch <= 16; ++ch)
        midiChannelBox.addItem ("CH " + juce::String (ch), ch);
    midiChannelAttach = std::make_unique<PolledComboBoxAttachment> (*apvts.getParameter (ParamIDs::midiOutCh), midiChannelBox);
    midiChannelBox.setTooltip (u8 ("Canal MIDI de salida (1-16) de los acordes hacia el plugin externo. "
                                   "El puerto MIDI se elige en el DAW (en FL Studio: engranaje del plugin > MIDI > Output port)."));
    content.addAndMakeVisible (midiChannelBox);

    setupToggle (microButton, "MICROTUNING", false, 11.0f);
    microButton.setTooltip (u8 ("Afina el piano interno con los cents reales de la escala (cuartos de tono, entonación justa)"));
    microAttach = std::make_unique<PolledButtonAttachment> (*apvts.getParameter (ParamIDs::microTuning), microButton);
    microButton.onStateChange();

    panicButton.getProperties().set (fontSizeProperty, 11.5f);
    panicButton.onClick = [this] { audioProcessor.requestPanic(); };
    content.addAndMakeVisible (panicButton);

    //--------------------------------------------------------------------------
    // Tira MIC: controlador de voz en tiempo real
    setupToggle (micButton, "MIC VOICE: ", true, 12.0f);
    micButton.setTooltip (u8 ("Tu voz dispara notas o acordes (según el modo) en tiempo real"));
    micAttach = std::make_unique<PolledButtonAttachment> (*apvts.getParameter (ParamIDs::micOn), micButton);
    micButton.onStateChange();

    // De dónde llega la voz (siempre el bus de entrada del plugin: nunca se
    // abren dispositivos de audio propios dentro del DAW)
    const bool standaloneApp = audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    voiceInputLabel.setText (SmartKeysAudioProcessor::isFxVariant
                                 ? u8 ("Voz: la pista del Mixer donde está SmartKeys FX")
                                 : standaloneApp ? u8 ("Voz: entrada de audio de la app (Options)")
                                                 : u8 ("Voz: entrada sidechain del plugin (o usa SmartKeys FX)"),
                             juce::dontSendNotification);
    voiceInputLabel.setFont (makeFont (11.5f, true));
    voiceInputLabel.setJustificationType (juce::Justification::centredLeft);
    voiceInputLabel.setMinimumHorizontalScale (0.7f);
    voiceInputLabel.setColour (juce::Label::textColourId, Colours::label);
    voiceInputLabel.setTooltip (SmartKeysAudioProcessor::isFxVariant
                                    ? u8 ("SmartKeys FX escucha la pista del Mixer donde está insertado (tu micrófono).")
                                    : u8 ("El instrumento escucha la voz por su entrada sidechain. En FL Studio lo más fácil es "
                                          "poner SmartKeys FX en la pista del micrófono del Mixer."));
    content.addAndMakeVisible (voiceInputLabel);

    // Puerto MIDI virtual "SmartKeys Chord Controller"
    setupToggle (portButton, "MIDI PORT: ", true, 11.5f);
    portButton.setToggleState (audioProcessor.isVirtualPortEnabled(), juce::dontSendNotification);
    portButton.onStateChange();
    portButton.onClick = [this] { audioProcessor.setVirtualPortEnabled (portButton.getToggleState()); };
    {
        const auto portName = juce::String (smartkeys::VirtualMidiPort::kPortName);
        portButton.setTooltip (u8 ("Crea el puerto MIDI \"") + portName + u8 ("\": el DAW lo ve como un teclado MIDI que toca "
                               "los acordes completos.\nFL Studio: Options > MIDI Settings > Input > ") + portName
                               + u8 (" > Enable y ponle un Port libre (p. ej. 10).\nLuego clic derecho en el canal del instrumento "
                                     "(FLEX, Serum...) > Receive notes from > ") + portName
                               + u8 (".\nCon SmartKeys seleccionado, toca y graba: ese instrumento recibe y graba el acorde completo."));
    }
    content.addAndMakeVisible (portButton);

    portStatusLabel.setFont (makeFont (11.0f, true));
    portStatusLabel.setJustificationType (juce::Justification::centredLeft);
    portStatusLabel.setMinimumHorizontalScale (0.65f);
    portStatusLabel.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (portStatusLabel);

    content.addAndMakeVisible (micMeter);

    micStatusLabel.setFont (makeFont (12.5f, true));
    micStatusLabel.setJustificationType (juce::Justification::centredLeft);
    micStatusLabel.setMinimumHorizontalScale (0.7f);
    micStatusLabel.setColour (juce::Label::textColourId, Colours::label);
    content.addAndMakeVisible (micStatusLabel);

    //--------------------------------------------------------------------------
    // Pestañas: KEY · SCALE  |  PROGRESIÓN
    tabKeys.setButtonText (u8 ("KEY · SCALE"));
    tabProgression.setButtonText (u8 ("PROGRESIÓN"));
    for (auto* t : { &tabKeys, &tabProgression })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (101);
        t->getProperties().set (fontSizeProperty, 12.0f);
        content.addAndMakeVisible (t);
    }
    tabKeys.setTooltip (u8 ("Tónica, escala y notas activas"));
    tabProgression.setTooltip (u8 ("Constructor de progresiones: carril por compases, paleta de acordes, grabación con el DAW y vista previa"));
    tabKeys.onClick = [this] { if (tabKeys.getToggleState()) showTab (Tab::KeyScale); };
    tabProgression.onClick = [this] { if (tabProgression.getToggleState()) showTab (Tab::Progression); };

    // Pestaña KEY · SCALE
    content.addAndMakeVisible (keySelector);
    content.addAndMakeVisible (scaleSelector);
    content.addAndMakeVisible (scaleBar);

    // Pestaña PROGRESIÓN
    previewButton.getProperties().set (fontSizeProperty, 13.0f);
    previewButton.setTooltip (u8 ("Escuchar la progresión en bucle (al tempo del proyecto)"));
    previewButton.onClick = [this]
    {
        if (audioProcessor.isPreviewPlaying()) audioProcessor.stopPreview();
        else                                   audioProcessor.startPreview();
    };
    content.addChildComponent (previewButton);

    recButton.setButtonText (u8 ("● REC"));
    recButton.setClickingTogglesState (true);
    recButton.getProperties().set (fontSizeProperty, 11.5f);
    recButton.setTooltip (u8 ("Grabar acordes ahora mismo aunque el DAW esté detenido (al tempo del proyecto). "
                              "Con el DAW en marcha se graba en su compás exacto."));
    recButton.onClick = [this] { audioProcessor.setRecArmed (recButton.getToggleState()); };
    content.addChildComponent (recButton);

    syncBox.addItem (u8 ("Captura: ▶ ●"), 1);
    syncBox.addItem (u8 ("Captura: ●"), 2);
    syncBox.addItem (u8 ("Captura: REC"), 3);
    syncBox.setTooltip (u8 ("Cuándo se guardan los acordes en la progresión:\n"
                            "▶ ●  al reproducir o grabar el DAW (convierte las notas del Piano Roll en acordes)\n"
                            "●  sólo cuando el DAW graba\n"
                            "REC  sólo con el botón ● REC del plugin\n"
                            "Con el DAW detenido, probar teclas NO guarda nada."));
    syncBox.onChange = [this] { audioProcessor.setCaptureSync (syncBox.getSelectedId() - 1); };
    content.addChildComponent (syncBox);

    takeBox.addItem (u8 ("Toma: nueva"), 1);
    takeBox.addItem (u8 ("Toma: añadir"), 2);
    takeBox.setTooltip (u8 ("Nueva: cada toma reemplaza la progresión al tocar el primer acorde (se puede deshacer).\n"
                            "Añadir: conserva lo anterior; lo que vuelvas a tocar encima se sobrescribe."));
    takeBox.onChange = [this] { audioProcessor.setTakeMode (takeBox.getSelectedId() - 1); };
    content.addChildComponent (takeBox);

    {
        const auto lengths = smartkeys::NewChordLength::getNames();
        for (int i = 0; i < lengths.size(); ++i)
            lengthBox.addItem (lengths[i], i + 1);
    }
    lengthBox.setTooltip (u8 ("Duración de los acordes que añades desde la paleta"));
    lengthBox.onChange = [this] { audioProcessor.setNewChordLengthIndex (lengthBox.getSelectedId() - 1); };
    content.addChildComponent (lengthBox);

    namesButton.setButtonText ("C  Dm");
    romanButton.setButtonText ("I  ii");
    for (auto* b : { &namesButton, &romanButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (102);
        b->getProperties().set (fontSizeProperty, 11.5f);
        content.addChildComponent (b);
    }
    namesButton.setTooltip (u8 ("Ver los acordes con su cifrado (C, Dm, Em...)"));
    romanButton.setTooltip (u8 ("Ver los acordes como grados romanos (I, ii, iii, IV, V, vi, vii°)"));
    namesButton.onClick = [this] { if (namesButton.getToggleState()) audioProcessor.setShowRoman (false); };
    romanButton.onClick = [this] { if (romanButton.getToggleState()) audioProcessor.setShowRoman (true); };

    {
        const auto grids = smartkeys::QuantizeGrid::getNames();
        for (int i = 0; i < grids.size(); ++i)
            quantizeBox.addItem (grids[i], i + 1);
    }
    quantizeBox.setTooltip (u8 ("QUANTIZE de la exportación (y rejilla para mover acordes en el carril). "
                                "El desfase del STRUM se conserva."));
    quantizeBox.onChange = [this] { audioProcessor.setQuantizeIndex (quantizeBox.getSelectedId() - 1); };
    content.addChildComponent (quantizeBox);

    undoButton.setButtonText (u8 ("↶"));
    undoButton.getProperties().set (fontSizeProperty, 15.0f);
    undoButton.setTooltip (u8 ("Deshacer el último cambio de la progresión (Cmd/Ctrl+Z)"));
    undoButton.onClick = [this] { lane.undo(); };
    content.addChildComponent (undoButton);

    clearProgressionButton.getProperties().set (fontSizeProperty, 11.0f);
    clearProgressionButton.setTooltip (u8 ("Borra toda la progresión (se puede deshacer)"));
    clearProgressionButton.onClick = [this]
    {
        audioProcessor.withProgression ([] (smartkeys::ProgressionModel& m) { m.clear(); });
        audioProcessor.progressionEdited();
    };
    content.addChildComponent (clearProgressionButton);

    content.addChildComponent (lane);
    content.addChildComponent (palette);

    //--------------------------------------------------------------------------
    // Teclado + acorde + STRUM + exportación
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
    strumKnob.setTooltip (u8 ("STRUM SPEED / NOTE ENTRY: retardo progresivo entre las notas del acorde (0 a 200 ms). "
                              "0 = todas juntas. Doble clic: OFF."));
    strumAttach = std::make_unique<PolledSliderAttachment> (*apvts.getParameter (ParamIDs::strumMs), strumKnob);
    strumKnob.setDoubleClickReturnValue (true, 0.0);
    content.addAndMakeVisible (strumKnob);

    strumValueLabel.setFont (makeFont (10.0f, true));
    strumValueLabel.setBorderSize ({ 0, 2, 0, 0 });
    strumValueLabel.setJustificationType (juce::Justification::centredLeft);
    strumValueLabel.setColour (juce::Label::textColourId, Colours::text);
    strumValueLabel.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (strumValueLabel);

    strumDirSwitch.setButtonFontSize (11.0f);
    strumDirSwitch.setTooltips ({ u8 ("ASC ▲: las notas entran de la más grave a la más aguda"),
                                  u8 ("DESC ▼: las notas entran de la más aguda a la más grave") });
    content.addAndMakeVisible (strumDirSwitch);

    dragButton.setTooltip (u8 ("Arrastra este botón al Piano Roll de FL Studio / Ableton (o a cualquier plugin): "
                               "suelta la progresión del carril con todas las notas de cada acorde en su compás exacto. "
                               "Clic: guarda el .mid en Música/SmartKeys y lo muestra."));
    content.addAndMakeVisible (dragButton);

    setWantsKeyboardFocus (true);
    computerKeyNotes.fill (-1);
    audioProcessor.editorOpened();   // con la ventana abierta ya puede crearse el puerto MIDI virtual

   #if JucePlugin_Build_Standalone
    // En la app Standalone JUCE silencia la entrada por defecto "para evitar
    // realimentación". SmartKeys nunca envía la voz a la salida, así que la
    // reactivamos para que el controlador vocal funcione de inmediato.
    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    showTab (Tab::KeyScale);

    // Ventana redimensionable (mantiene la proporción); recuerda el tamaño del proyecto.
    // La escala guardada se lee ANTES de fijar los límites: setResizeLimits() ajusta
    // el editor (aún de 0×0) al tamaño mínimo y resized() guardaría esa escala.
    const float savedScale = audioProcessor.getEditorScale();
    setResizable (true, true);
    setResizeLimits ((int) (kWidth * 0.7f), (int) (kHeight * 0.7f), kWidth * 2, kHeight * 2);
    if (auto* editorConstrainer = getConstrainer())
        editorConstrainer->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    setSize (juce::roundToInt ((float) kWidth * savedScale), juce::roundToInt ((float) kHeight * savedScale));

    timerCallback();
    startTimerHz (60);
}

SmartKeysAudioProcessorEditor::~SmartKeysAudioProcessorEditor()
{
    stopTimer();
    for (size_t i = 0; i < computerKeysDown.size(); ++i)
        if (computerKeysDown[i] && computerKeyNotes[i] >= 0)
            audioProcessor.pushScreenNote (computerKeyNotes[i], 0, false);
    audioProcessor.auditionChord (nullptr, 0, 0, false);
    setLookAndFeel (nullptr);
}

void SmartKeysAudioProcessorEditor::setRefPitch (float hz)
{
    if (auto* param = apvts.getParameter (ParamIDs::refPitch))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (hz));
        param->endChangeGesture();
    }
}

juce::String SmartKeysAudioProcessorEditor::makeExportBaseName() const
{
    return "SmartKeys - " + displayEngine.getScaleDisplayName() + " - "
           + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
}

void SmartKeysAudioProcessorEditor::showTab (Tab tab)
{
    currentTab = tab;
    const bool keys = tab == Tab::KeyScale;

    tabKeys.setToggleState (keys, juce::dontSendNotification);
    tabProgression.setToggleState (! keys, juce::dontSendNotification);

    for (juce::Component* c : { (juce::Component*) &keySelector, (juce::Component*) &scaleSelector, (juce::Component*) &scaleBar })
        c->setVisible (keys);

    for (juce::Component* c : { (juce::Component*) &previewButton, (juce::Component*) &recButton, (juce::Component*) &syncBox,
                                (juce::Component*) &takeBox, (juce::Component*) &lengthBox, (juce::Component*) &namesButton,
                                (juce::Component*) &romanButton, (juce::Component*) &quantizeBox, (juce::Component*) &undoButton,
                                (juce::Component*) &clearProgressionButton, (juce::Component*) &lane, (juce::Component*) &palette })
        c->setVisible (! keys);

    content.repaint();
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

    // Título
    g.setColour (Colours::text);
    g.setFont (makeFont (22.0f, true));
    g.drawText ("SMARTKEYS", 18, 12, 140, 26, juce::Justification::centredLeft, false);
    if (SmartKeysAudioProcessor::isFxVariant)
    {
        drawGlowBox (g, { 150.0f, 16.0f, 28.0f, 17.0f }, 4.0f, Colours::accent, false);
        g.setColour (Colours::white);
        g.setFont (makeFont (11.0f, true));
        g.drawText ("FX", 150, 16, 28, 17, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (Colours::accent);
        g.fillEllipse (150.0f, 22.0f, 7.0f, 7.0f);
    }
    g.setColour (Colours::label);
    g.setFont (makeFont (10.0f, true));
    g.drawText (u8 ("PIANO  ·  CHORDS  ·  VOICE"), 18, 36, 170, 12, juce::Justification::centredLeft, false);

    // Rótulos de las filas superiores
    g.setFont (makeFont (8.5f, true));
    g.drawText ("M O D E", 194, 2, 120, 10, juce::Justification::centredLeft, false);
    g.drawText ("O U T P U T   M O D E", 590, 2, 200, 10, juce::Justification::centredLeft, false);

    g.setFont (makeFont (11.0f, true));
    g.drawText ("A4", 18, 54, 26, 28, juce::Justification::centredLeft, false);
    g.drawText ("MIDI", 244, 54, 40, 28, juce::Justification::centredLeft, false);
    g.drawText ("GAIN", 634, 54, 36, 28, juce::Justification::centredLeft, false);

    // Tarjetas
    auto card = [&] (juce::Rectangle<float> r)
    {
        g.setColour (Colours::white);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::border.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    };
    card ({ 16.0f, 90.0f, 818.0f, 62.0f });       // MIC
    card ({ 16.0f, 458.0f, 818.0f, 218.0f });     // LIVE KEYBOARD

    // Rótulo del GATE (dentro de la tarjeta MIC: se dibuja después de ella)
    g.setColour (Colours::label);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("GATE", 220, 124, 34, 24, juce::Justification::centredLeft, false);

    if (currentTab == Tab::KeyScale)
    {
        card ({ 16.0f, 188.0f, 410.0f, 170.0f });   // KEY
        card ({ 436.0f, 188.0f, 398.0f, 170.0f });  // SCALE
        card ({ 16.0f, 366.0f, 818.0f, 84.0f });    // ESCALA

        // Pestaña de acento sobre KEY (como en la referencia)
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (16.0f, 188.0f, 28.0f, 3.0f, 1.5f);
        g.setColour (Colours::label);
        g.setFont (makeFont (13.0f, true));
        g.drawText ("K E Y", 30, 194, 120, 18, juce::Justification::centredLeft, false);
    }
    else
    {
        card ({ 16.0f, 188.0f, 818.0f, 262.0f });   // PROGRESIÓN

        // Icono de rejilla junto al QUANTIZE (como en Klimper)
        const juce::Rectangle<float> icon (594.0f, 200.0f, 12.0f, 12.0f);
        g.setColour (Colours::charcoal.withAlpha (0.7f));
        for (int i = 0; i < 3; ++i)
        {
            g.fillRect (icon.getX() + (float) i * 5.0f, icon.getY(), 1.4f, icon.getHeight());
            g.fillRect (icon.getX(), icon.getY() + (float) i * 5.0f, icon.getWidth(), 1.4f);
        }
    }

    // Estado de la captura (a la derecha de las pestañas)
    juce::Colour statusColour;
    const auto status = getCaptureStatusText (statusColour);
    g.setColour (statusColour);
    g.setFont (makeFont (11.5f, true));
    g.drawText (status, 350, 158, 484, 26, juce::Justification::centredRight, false);

    g.setColour (Colours::label);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("L I V E   K E Y B O A R D", 30, 464, 260, 16, juce::Justification::centredLeft, false);
    g.setFont (makeFont (10.0f, true));
    g.drawText (u8 ("Teclado PC: A W S E D F T G Y H U J K  ·  Z / X octava"), 270, 464, 306, 16,
                juce::Justification::centredRight, false);
}

juce::String SmartKeysAudioProcessorEditor::getCaptureStatusText (juce::Colour& colour) const
{
    if (audioProcessor.isCapturing())
    {
        const double head = audioProcessor.withProgression ([] (smartkeys::ProgressionModel& m) { return m.getCaptureHead(); });
        const int bar = (int) std::floor (juce::jmax (0.0, head) / juce::jmax (1.0, audioProcessor.getBeatsPerBar())) + 1;
        colour = Colours::recording;
        return audioProcessor.isCapturingFromHost()
                 ? u8 ("● GRABANDO DESDE EL DAW · compás ") + juce::String (bar)
                 : u8 ("● REC · grabando a ") + juce::String (audioProcessor.getHostBpm(), 0) + " BPM";
    }

    colour = Colours::label;
    switch (audioProcessor.getCaptureSync())
    {
        case CaptureSync::RecordOnly: return u8 ("Captura: al grabar (●) en el DAW");
        case CaptureSync::ManualOnly: return u8 ("Captura: sólo con ● REC");
        default:                      return u8 ("Captura: al dar ▶ o ● en el DAW  ·  detenido no se guarda nada");
    }
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
    // Fila 1: título | SMART CHORDS · SINGLE NOTES · COMPLEX CHORDS | PIANO + MIDI · MIDI OUT
    modeSwitch.setBounds (190, 12, 390, 34);
    outputSwitch.setBounds (586, 12, 248, 34);

    // Fila 2: afinación | canal MIDI | microafinación | pánico | ganancia
    tuningSlider.setBounds (44, 54, 100, 28);
    tune440.setBounds (146, 54, 44, 28);
    tune442.setBounds (190, 54, 44, 28);
    midiChannelBox.setBounds (284, 56, 76, 24);
    microButton.setBounds (366, 54, 114, 28);
    panicButton.setBounds (484, 54, 142, 28);
    gainSlider.setBounds (670, 54, 164, 28);

    // Tira MIC + PUERTO MIDI
    micButton.setBounds (22, 94, 150, 28);
    voiceInputLabel.setBounds (180, 94, 356, 28);
    portButton.setBounds (544, 94, 138, 28);
    portStatusLabel.setBounds (688, 94, 142, 28);
    micMeter.setBounds (26, 128, 186, 16);
    gateSlider.setBounds (256, 124, 160, 24);
    micStatusLabel.setBounds (424, 124, 404, 24);

    // Pestañas
    tabKeys.setBounds (16, 158, 160, 26);
    tabProgression.setBounds (180, 158, 160, 26);

    // KEY · SCALE
    keySelector.setBounds (30, 216, 382, 132);
    scaleSelector.setBounds (448, 194, 374, 158);
    scaleBar.setBounds (24, 370, 802, 74);

    // PROGRESIÓN
    previewButton.setBounds (24, 194, 32, 26);
    recButton.setBounds (60, 194, 62, 26);
    syncBox.setBounds (126, 195, 136, 24);
    takeBox.setBounds (266, 195, 124, 24);
    lengthBox.setBounds (394, 195, 100, 24);
    namesButton.setBounds (498, 194, 46, 26);
    romanButton.setBounds (544, 194, 46, 26);
    quantizeBox.setBounds (610, 195, 112, 24);
    undoButton.setBounds (726, 194, 32, 26);
    clearProgressionButton.setBounds (762, 194, 64, 26);
    lane.setBounds (24, 226, 802, 94);
    palette.setBounds (24, 326, 802, 120);

    // Teclado + display + selector + STRUM + exportación
    piano.setBounds (24, 486, 552, 182);
    chordDisplay.setBounds (590, 466, 236, 76);
    chordTypeSwitch.setBounds (587, 546, 242, 28);
    variationSwitch.setBounds (587, 546, 242, 28);
    strumKnob.setBounds (587, 578, 40, 40);
    strumValueLabel.setBounds (629, 578, 74, 40);
    strumDirSwitch.setBounds (705, 584, 124, 28);
    dragButton.setBounds (587, 624, 242, 44);
}

//==============================================================================
void SmartKeysAudioProcessorEditor::timerCallback()
{
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

    updatePortStatus();
    updateMicStatus();
    updateProgressionControls();
    dragButton.setCount (audioProcessor.getNumProgressionChords(), audioProcessor.isProgressionFull(),
                         audioProcessor.isCapturing());
    updateDisplay();
}

void SmartKeysAudioProcessorEditor::updateProgressionControls()
{
    // Ajustes (también si el host cargó otro proyecto)
    if (syncBox.getSelectedId() != audioProcessor.getCaptureSync() + 1)
        syncBox.setSelectedId (audioProcessor.getCaptureSync() + 1, juce::dontSendNotification);
    if (takeBox.getSelectedId() != audioProcessor.getTakeMode() + 1)
        takeBox.setSelectedId (audioProcessor.getTakeMode() + 1, juce::dontSendNotification);
    if (lengthBox.getSelectedId() != audioProcessor.getNewChordLengthIndex() + 1)
        lengthBox.setSelectedId (audioProcessor.getNewChordLengthIndex() + 1, juce::dontSendNotification);
    if (quantizeBox.getSelectedId() != audioProcessor.getQuantizeIndex() + 1)
        quantizeBox.setSelectedId (audioProcessor.getQuantizeIndex() + 1, juce::dontSendNotification);

    const bool roman = audioProcessor.getShowRoman();
    if (romanButton.getToggleState() != roman)
        romanButton.setToggleState (roman, juce::dontSendNotification);
    if (namesButton.getToggleState() == roman)
        namesButton.setToggleState (! roman, juce::dontSendNotification);

    const bool armed = audioProcessor.isRecArmed();
    if (recButton.getToggleState() != armed)
        recButton.setToggleState (armed, juce::dontSendNotification);

    const auto previewText = audioProcessor.isPreviewPlaying() ? u8 ("■") : u8 ("▶");
    if (previewButton.getButtonText() != previewText)
        previewButton.setButtonText (previewText);

    // Valor de la perilla STRUM
    const double strumMs = audioProcessor.getStrumMs();
    const auto strumText = "STRUM SPEED\n" + (strumMs < 0.05 ? juce::String ("OFF")
                                       : (strumMs < 10.0 ? juce::String (strumMs, 1) : juce::String (juce::roundToInt (strumMs))) + " ms");
    if (strumValueLabel.getText() != strumText)
        strumValueLabel.setText (strumText, juce::dontSendNotification);

    // Estado de la captura (cambia el texto de la fila de pestañas)
    juce::Colour statusColour;
    const auto status = getCaptureStatusText (statusColour);
    if (status != shownCaptureStatus)
    {
        shownCaptureStatus = status;
        content.repaint (350, 158, 484, 26);
    }

    if (currentTab == Tab::Progression)
    {
        palette.refresh (displayEngine.getRootKey(), displayEngine.getCustomMask(), roman, displayEngine);
        lane.update (roman, displayEngine);

        if (++blinkCounter >= 6)
        {
            blinkCounter = 0;
            undoButton.setEnabled (audioProcessor.withProgression ([] (smartkeys::ProgressionModel& m) { return m.canUndo(); }));
            clearProgressionButton.setEnabled (audioProcessor.getNumProgressionChords() > 0);
        }
    }
}

void SmartKeysAudioProcessorEditor::updateMicStatus()
{
    const bool micOn = apvts.getRawParameterValue (ParamIDs::micOn)->load() > 0.5f;
    const auto& tracker = audioProcessor.getVoiceTracker();
    const bool standalone = audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;

    juce::String text;
    auto colour = Colours::label;

    if (! audioProcessor.isMicSourceAvailable() || audioProcessor.getMicLevelDb() < -90.0f)
    {
        text = SmartKeysAudioProcessor::isFxVariant ? u8 ("Sin señal en la pista: canta o revisa la entrada del Mixer")
             : standalone ? u8 ("Sin señal: elige tu micrófono en Options > Audio/MIDI Settings")
                          : u8 ("Sin señal: manda el mic al sidechain o usa SmartKeys FX en su pista");
    }
    else if (! tracker.gateOpen.load())
    {
        text = u8 ("Mic: por debajo del GATE (canta más fuerte o baja el GATE)");
    }
    else
    {
        const float detected = tracker.detectedMidiNote.load();
        if (detected < 0.0f)
        {
            text = u8 ("Mic: escuchando...");
        }
        else
        {
            const auto noteName = displayEngine.getNoteName ((int) std::lround (detected));
            if (! micOn)
            {
                text = "Mic: " + noteName + u8 ("  ·  activa MIC VOICE para disparar acordes");
            }
            else if (audioProcessor.isVoiceChordSounding())
            {
                const auto ev = smartkeys::MidiOutputRouter::decodeLastEvent (audioProcessor.getLastVoiceEventPacked());
                text = "Mic: " + noteName + " -> " + displayEngine.getChordName (ev.notes);
                colour = Colours::accent;
            }
            else
            {
                text = "Mic: " + noteName;
                colour = Colours::accent;
            }
        }
    }

    if (micStatusLabel.getText() != text)
        micStatusLabel.setText (text, juce::dontSendNotification);
    if (micStatusLabel.findColour (juce::Label::textColourId) != colour)
        micStatusLabel.setColour (juce::Label::textColourId, colour);
}

void SmartKeysAudioProcessorEditor::updatePortStatus()
{
    // El botón refleja el proyecto cargado (el host puede cambiarlo con la ventana abierta)
    const bool enabled = audioProcessor.isVirtualPortEnabled();
    if (portButton.getToggleState() != enabled)
    {
        portButton.setToggleState (enabled, juce::dontSendNotification);
        portButton.onStateChange();
    }

    const int state = audioProcessor.getVirtualPortState();
    if (state == shownPortState)
        return;
    shownPortState = state;

    juce::String text;
    auto colour = Colours::label;
    switch (state)
    {
        case PortState::Active:      text = u8 ("● Chord Controller activo");  colour = juce::Colour (0xFF12B76A); break;
       #if JUCE_WINDOWS
        case PortState::Unavailable: text = u8 ("Sólo macOS (usa el MIDI Out)"); colour = Colours::warning;       break;
       #else
        // macOS no dejó crear el puerto: casi siempre porque la app SmartKeys u otro
        // DAW con SmartKeys ya lo tiene abierto. El procesador reintenta solo.
        case PortState::Unavailable: text = u8 ("En uso por otra app");         colour = Colours::warning;       break;
       #endif
        case PortState::Waiting:     text = u8 ("Esperando al DAW...");                                             break;
        default:                     text = u8 ("Puerto apagado");                                                  break;
    }
    portStatusLabel.setText (text, juce::dontSendNotification);
    portStatusLabel.setColour (juce::Label::textColourId, colour);
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
    if (ev.mode == smartkeys::MidiOutputRouter::kExplicitMode)
    {
        detail = u8 ("Progresión  ->  ") + notes;
    }
    else if (ev.fromVoice)
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
