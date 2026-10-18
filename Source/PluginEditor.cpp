/*
  ==============================================================================

    PluginEditor.cpp
    SmartKeys — Interfaz gráfica vectorial (850 x 640 px).

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
    constexpr int kWhitePcs[7]   = { 0, 2, 4, 5, 7, 9, 11 };
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

void DragChordsButton::setCount (int numChords, bool isFull)
{
    bool needsRepaint = (numChords != count || isFull != full);

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
    return processor.exportCapturedChords (makeBaseName != nullptr ? makeBaseName() : juce::String ("SmartKeys Chords"));
}

} // namespace smartkeys::ui

//==============================================================================
// EDITOR
//==============================================================================
using namespace smartkeys::ui;

namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
}

SmartKeysAudioProcessorEditor::SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      apvts (p.getValueTreeState()),
      modeSwitch (*apvts.getParameter (ParamIDs::mode), { "SMART CHORDS", "SINGLE NOTES", "COMPLEX CHORDS" }),
      outputSwitch (*apvts.getParameter (ParamIDs::outputMode), { "PIANO", "MIDI OUT", "BOTH" }),
      sourceSwitch (*apvts.getParameter (ParamIDs::micSource), { "DAW INPUT", "DEVICE" }),
      keySelector (*apvts.getParameter (ParamIDs::rootKey)),
      scaleSelector (*apvts.getParameter (ParamIDs::scale)),
      scaleBar (p, *apvts.getParameter (ParamIDs::scale)),
      piano (p),
      chordTypeSwitch (*apvts.getParameter (ParamIDs::chordExt), { "TRIAD", "+8VA", "7TH" }),
      variationSwitch (*apvts.getParameter (ParamIDs::complexVar), { "LUSH 7/9", "NEO-SOUL", "CINEMATIC" }),
      dragButton (p, [this] { return makeExportBaseName(); })
{
    setLookAndFeel (&lookAndFeel);

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
        addAndMakeVisible (b);
    };

    //--------------------------------------------------------------------------
    // Fila 1: modo + OUTPUT MODE
    modeSwitch.setButtonFontSize (12.5f);
    modeSwitch.setTooltips ({ u8 ("1 tecla = acorde diatónico (tríada / +8va / 7ª)"),
                              u8 ("1 tecla = 1 nota corregida a la escala"),
                              u8 ("1 tecla = acorde de 5 a 7 notas a dos manos (R&B / Neo-Soul)") });
    addAndMakeVisible (modeSwitch);

    outputSwitch.setButtonFontSize (11.5f);
    outputSwitch.setTooltips ({ u8 ("INTERNAL PIANO: sólo suena el piano de SmartKeys (no sale MIDI)"),
                                u8 ("EXTERNAL PLUGIN (MIDI OUT): los acordes salen por MIDI hacia otro plugin; el piano interno se silencia"),
                                u8 ("BOTH: piano interno + acordes por la salida MIDI") });
    addAndMakeVisible (outputSwitch);

    //--------------------------------------------------------------------------
    // Fila 2: afinación, canal MIDI, microafinación, pánico, ganancia
    for (auto* s : { &gainSlider, &gateSlider, &tuningSlider })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        s->setColour (juce::Slider::textBoxTextColourId, Colours::text);
        addAndMakeVisible (s);
    }
    gainAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::masterGain, gainSlider);
    gateAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::micGate, gateSlider);
    tuningAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::refPitch, tuningSlider);

    tune440.onClick = [this] { setRefPitch (440.0f); };
    tune442.onClick = [this] { setRefPitch (442.0f); };
    addAndMakeVisible (tune440);
    addAndMakeVisible (tune442);

    for (int ch = 1; ch <= 16; ++ch)
        midiChannelBox.addItem ("CH " + juce::String (ch), ch);
    midiChannelAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, ParamIDs::midiOutCh, midiChannelBox);
    midiChannelBox.setTooltip (u8 ("Canal MIDI de salida (1-16) de los acordes hacia el plugin externo. "
                                   "El puerto MIDI se elige en el DAW (en FL Studio: ajustes del wrapper > MIDI > Output port)."));
    addAndMakeVisible (midiChannelBox);

    setupToggle (microButton, "MICROTUNING", false, 11.0f);
    microButton.setTooltip (u8 ("Afina el piano interno con los cents reales de la escala (cuartos de tono, entonación justa)"));
    microAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamIDs::microTuning, microButton);
    microButton.onStateChange();

    panicButton.getProperties().set (fontSizeProperty, 11.5f);
    panicButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible (panicButton);

    //--------------------------------------------------------------------------
    // Tira MIC: controlador de voz en tiempo real
    setupToggle (micButton, "MIC VOICE: ", true, 12.0f);
    micButton.setTooltip (u8 ("Tu voz dispara notas o acordes (según el modo) en tiempo real"));
    micAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamIDs::micOn, micButton);
    micButton.onStateChange();

    sourceSwitch.setButtonFontSize (11.5f);
    sourceSwitch.setTooltips ({ u8 (SmartKeysAudioProcessor::isFxVariant
                                        ? "DAW INPUT: la voz de la pista donde está insertado SmartKeys FX"
                                        : "DAW INPUT: la voz llega por la entrada sidechain del plugin (ruteo del DAW)"),
                                u8 ("DEVICE: SmartKeys lee el micrófono directamente de tu interfaz de audio, sin ruteos") });
    addAndMakeVisible (sourceSwitch);

    deviceBox.setTooltip (u8 ("Micrófono / interfaz de audio que SmartKeys escucha directamente (vía DEVICE)"));
    deviceBox.onChange = [this] { applyDeviceSelection(); };
    addAndMakeVisible (deviceBox);

    channelBox.addItem ("CH 1", 1);
    channelBox.addItem ("CH 2", 2);
    channelBox.addItem ("CH 1+2", 3);
    channelBox.setSelectedId (processor.getHardwareChannelMode() + 1, juce::dontSendNotification);
    channelBox.setTooltip (u8 ("Canal de la interfaz donde está conectado el micrófono"));
    channelBox.onChange = [this] { applyDeviceSelection(); };
    addAndMakeVisible (channelBox);

    rescanButton.getProperties().set (fontSizeProperty, 11.0f);
    rescanButton.setTooltip (u8 ("Vuelve a buscar micrófonos e interfaces conectados"));
    rescanButton.onClick = [this]
    {
        refreshDeviceList();
        processor.retryHardwareInput();
    };
    addAndMakeVisible (rescanButton);

    addAndMakeVisible (micMeter);

    micStatusLabel.setFont (makeFont (12.5f, true));
    micStatusLabel.setJustificationType (juce::Justification::centredLeft);
    micStatusLabel.setMinimumHorizontalScale (0.7f);
    micStatusLabel.setColour (juce::Label::textColourId, Colours::label);
    addAndMakeVisible (micStatusLabel);

    refreshDeviceList();

    //--------------------------------------------------------------------------
    // Módulos
    addAndMakeVisible (keySelector);
    addAndMakeVisible (scaleSelector);
    addAndMakeVisible (scaleBar);
    addAndMakeVisible (piano);
    addAndMakeVisible (chordDisplay);

    chordTypeSwitch.setButtonFontSize (12.0f);
    chordTypeSwitch.setTooltips ({ u8 ("Tríada: grados k, k+2, k+4"),
                                   u8 ("Tríada + octava: añade k+7"),
                                   u8 ("Cuatriada: añade la 7ª (k+6)") });
    addAndMakeVisible (chordTypeSwitch);

    variationSwitch.setButtonFontSize (11.0f);
    variationSwitch.setTooltips (smartkeys::ScaleChordEngine::getComplexVariationNames());
    addChildComponent (variationSwitch);   // visible sólo en COMPLEX CHORDS

    //--------------------------------------------------------------------------
    // Grabador de acordes
    dragButton.setTooltip (u8 ("Arrastra este botón al Piano Roll de FL Studio / Ableton (o a cualquier plugin): "
                               "suelta todos los acordes que tocaste o cantaste, con todas sus notas. "
                               "Clic: guarda el .mid en Música/SmartKeys y lo muestra."));
    addAndMakeVisible (dragButton);

    quantizeButton.setClickingTogglesState (true);
    quantizeButton.getProperties().set (fontSizeProperty, 11.0f);
    quantizeButton.setToggleState (processor.getExportQuantize(), juce::dontSendNotification);
    quantizeButton.setTooltip (u8 ("Ajusta el inicio y el final de cada acorde a semicorcheas al exportar"));
    quantizeButton.onClick = [this] { processor.setExportQuantize (quantizeButton.getToggleState()); };
    addAndMakeVisible (quantizeButton);

    clearButton.getProperties().set (fontSizeProperty, 11.0f);
    clearButton.setTooltip (u8 ("Borra la progresión grabada y empieza una nueva"));
    clearButton.onClick = [this] { processor.clearCapturedChords(); };
    addAndMakeVisible (clearButton);

    setWantsKeyboardFocus (true);
    computerKeyNotes.fill (-1);

   #if JucePlugin_Build_Standalone
    // En la app Standalone JUCE silencia la entrada por defecto "para evitar
    // realimentación". SmartKeys nunca envía la voz a la salida, así que la
    // reactivamos para que el controlador vocal funcione de inmediato.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    setSize (850, 640);
    timerCallback();
    startTimerHz (60);
}

SmartKeysAudioProcessorEditor::~SmartKeysAudioProcessorEditor()
{
    stopTimer();
    for (size_t i = 0; i < computerKeysDown.size(); ++i)
        if (computerKeysDown[i] && computerKeyNotes[i] >= 0)
            processor.pushScreenNote (computerKeyNotes[i], 0, false);
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

void SmartKeysAudioProcessorEditor::refreshDeviceList()
{
    shownDevices = processor.getHardwareInputDevices();
    deviceBox.clear (juce::dontSendNotification);

    if (shownDevices.isEmpty())
    {
        deviceBox.setTextWhenNothingSelected ("Sin entradas de audio");
        return;
    }

    deviceBox.setTextWhenNothingSelected (u8 ("Elige tu micrófono / interfaz..."));
    for (int i = 0; i < shownDevices.size(); ++i)
        deviceBox.addItem (shownDevices[i], i + 1);

    const int index = shownDevices.indexOf (processor.getHardwareDeviceName());
    if (index >= 0)
        deviceBox.setSelectedId (index + 1, juce::dontSendNotification);
}

void SmartKeysAudioProcessorEditor::applyDeviceSelection()
{
    const int id = deviceBox.getSelectedId();
    if (id <= 0 || id > shownDevices.size())
        return;
    processor.setHardwareInput (shownDevices[id - 1], juce::jmax (0, channelBox.getSelectedId() - 1), true);
}

//==============================================================================
void SmartKeysAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Línea de acento superior
    g.setColour (Colours::accent);
    g.fillRect (0, 0, 64, 3);
    g.setColour (Colours::border);
    g.fillRect (64, 0, getWidth() - 64, 1);

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
    card ({ 16.0f, 90.0f, 818.0f, 62.0f });    // MIC
    card ({ 16.0f, 160.0f, 410.0f, 170.0f });  // KEY
    card ({ 436.0f, 160.0f, 398.0f, 170.0f }); // SCALE
    card ({ 16.0f, 338.0f, 818.0f, 84.0f });   // ESCALA
    card ({ 16.0f, 430.0f, 818.0f, 194.0f });  // LIVE KEYBOARD

    // Rótulo del GATE (dentro de la tarjeta MIC: se dibuja después de ella)
    g.setColour (Colours::label);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("GATE", 220, 124, 34, 24, juce::Justification::centredLeft, false);

    // Pestaña de acento sobre KEY (como en la referencia)
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (16.0f, 160.0f, 28.0f, 3.0f, 1.5f);

    g.setColour (Colours::label);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("K E Y", 30, 166, 120, 18, juce::Justification::centredLeft, false);
    g.drawText ("L I V E   K E Y B O A R D", 30, 436, 260, 16, juce::Justification::centredLeft, false);
    g.setFont (makeFont (10.0f, true));
    g.drawText (u8 ("Teclado PC: A W S E D F T G Y H U J K  ·  Z / X octava"), 270, 436, 306, 16,
                juce::Justification::centredRight, false);
}

void SmartKeysAudioProcessorEditor::resized()
{
    // Fila 1: título | SMART CHORDS · SINGLE NOTES · COMPLEX CHORDS | PIANO · MIDI OUT · BOTH
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

    // Tira MIC
    micButton.setBounds (22, 94, 150, 28);
    sourceSwitch.setBounds (174, 94, 180, 28);
    deviceBox.setBounds (360, 96, 296, 24);
    channelBox.setBounds (662, 96, 80, 24);
    rescanButton.setBounds (746, 94, 82, 28);
    micMeter.setBounds (26, 128, 186, 16);
    gateSlider.setBounds (256, 124, 160, 24);
    micStatusLabel.setBounds (424, 124, 404, 24);

    // Módulos
    keySelector.setBounds (30, 188, 382, 132);
    scaleSelector.setBounds (448, 166, 374, 158);
    scaleBar.setBounds (24, 342, 802, 74);

    // Teclado + display + selector + grabador de acordes
    piano.setBounds (24, 456, 552, 160);
    chordDisplay.setBounds (590, 438, 236, 78);
    chordTypeSwitch.setBounds (587, 520, 242, 30);
    variationSwitch.setBounds (587, 520, 242, 30);
    dragButton.setBounds (587, 553, 242, 34);
    quantizeButton.setBounds (587, 590, 140, 28);
    clearButton.setBounds (727, 590, 102, 28);
}

//==============================================================================
void SmartKeysAudioProcessorEditor::timerCallback()
{
    const int root  = juce::jlimit (0, 11, (int) apvts.getRawParameterValue (ParamIDs::rootKey)->load());
    const int scale = juce::jlimit (0, smartkeys::kNumScales - 1, (int) apvts.getRawParameterValue (ParamIDs::scale)->load());
    const uint16_t mask = processor.getCustomMask();

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

    piano.setActiveNotes (processor.getActiveNotesLo(), processor.getActiveNotesHi());

    // Micrófono: medidor, dispositivo y estado en vivo
    const int source = juce::jlimit (0, 1, juce::roundToInt (apvts.getRawParameterValue (ParamIDs::micSource)->load()));
    const auto& tracker = processor.getActiveVoiceTracker();
    micMeter.setState (processor.getMicLevelDb(), apvts.getRawParameterValue (ParamIDs::micGate)->load(),
                       tracker.gateOpen.load(), processor.isMicSourceAvailable());

    const bool deviceSource = source == MicSource::HardwareDevice;
    deviceBox.setAlpha (deviceSource ? 1.0f : 0.6f);
    channelBox.setAlpha (deviceSource ? 1.0f : 0.6f);

    if (deviceSource && ! autoDeviceAttempted && processor.getHardwareDeviceName().isEmpty())
    {
        // Primera vez en DEVICE: proponer la entrada por defecto del sistema
        autoDeviceAttempted = true;
        const int index = shownDevices.indexOf (processor.getDefaultHardwareInputDevice());
        if (index >= 0)
            deviceBox.setSelectedId (index + 1, juce::sendNotificationSync);
    }
    if (! deviceSource)
        autoDeviceAttempted = false;

    // Si el host carga otro proyecto con la ventana abierta, los selectores lo reflejan
    if (++stateSyncCounter >= 15)
    {
        stateSyncCounter = 0;
        const int deviceIndex = shownDevices.indexOf (processor.getHardwareDeviceName());
        if (deviceIndex >= 0 && deviceBox.getSelectedId() != deviceIndex + 1)
            deviceBox.setSelectedId (deviceIndex + 1, juce::dontSendNotification);
        const int channelId = processor.getHardwareChannelMode() + 1;
        if (channelBox.getSelectedId() != channelId)
            channelBox.setSelectedId (channelId, juce::dontSendNotification);
        if (quantizeButton.getToggleState() != processor.getExportQuantize())
            quantizeButton.setToggleState (processor.getExportQuantize(), juce::dontSendNotification);
    }

    updateMicStatus();
    dragButton.setCount (processor.getNumCapturedChords(), processor.isCaptureFull());
    updateDisplay();
}

void SmartKeysAudioProcessorEditor::updateMicStatus()
{
    const bool micOn = apvts.getRawParameterValue (ParamIDs::micOn)->load() > 0.5f;
    const int source = juce::jlimit (0, 1, juce::roundToInt (apvts.getRawParameterValue (ParamIDs::micSource)->load()));
    const auto& tracker = processor.getActiveVoiceTracker();
    const bool standalone = processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;

    juce::String text;
    auto colour = Colours::label;

    if (source == MicSource::HardwareDevice && ! processor.isHardwareMicOpen())
    {
        const auto status = processor.getHardwareStatus();
        text = status.isNotEmpty() ? u8 ("No se pudo abrir el micrófono: ") + status
                                   : u8 ("Elige tu micrófono en la lista de arriba");
        colour = status.isNotEmpty() ? Colours::warning : Colours::label;
    }
    else if (source == MicSource::HardwareDevice
             && processor.getHardwareMsSinceStart() > 2500.0 && processor.getHardwareMsSinceSignal() > 2500.0)
    {
        text = standalone ? u8 ("Sin señal: permite el micrófono a SmartKeys (Ajustes > Privacidad y seguridad > Micrófono)")
                          : u8 ("Sin señal: permite el micrófono a tu DAW (Ajustes > Privacidad y seguridad > Micrófono)");
        colour = Colours::warning;
    }
    else if (! processor.isMicSourceAvailable() || processor.getMicLevelDb() < -90.0f)
    {
        if (source == MicSource::DawInput)
            text = SmartKeysAudioProcessor::isFxVariant ? u8 ("Sin señal en la pista: canta o revisa la entrada del Mixer")
                 : standalone ? u8 ("Sin señal en la entrada (Options): pulsa DEVICE para leer el micrófono directo")
                              : u8 ("Sin señal del DAW: pulsa DEVICE para leer el micrófono directo");
        else
            text = u8 ("Mic: silencio");
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
            else if (processor.isVoiceChordSounding())
            {
                const auto ev = smartkeys::MidiOutputRouter::decodeLastEvent (processor.getLastVoiceEventPacked());
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

void SmartKeysAudioProcessorEditor::updateDisplay()
{
    const uint64_t packed = processor.getLastEventPacked();
    const uint64_t lo = processor.getActiveNotesLo();
    const uint64_t hi = processor.getActiveNotesHi();
    const float detected = processor.getActiveVoiceTracker().detectedMidiNote.load();
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
    juce::String detail;

    if (ev.fromVoice)
    {
        const int shownInput = voiceNote >= 0 ? voiceNote : ev.triggerNote;
        detail = "Voice Input: " + displayEngine.getNoteName (shownInput) + " -> " + name;
    }
    else
    {
        juce::String notes;
        for (int n : ev.notes)
            notes << (notes.isEmpty() ? "" : " ") << displayEngine.getNoteName (n);
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

    for (int i = 0; i < 13; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) kKeyMap[i]);
        if (down == computerKeysDown[(size_t) i])
            continue;

        computerKeysDown[(size_t) i] = down;
        handled = true;

        if (down)
        {
            const int note = juce::jlimit (0, 127, (computerOctave + 1) * 12 + i);
            computerKeyNotes[(size_t) i] = note;
            processor.pushScreenNote (note, 100, true);
        }
        else if (computerKeyNotes[(size_t) i] >= 0)
        {
            processor.pushScreenNote (computerKeyNotes[(size_t) i], 0, false);
            computerKeyNotes[(size_t) i] = -1;
        }
    }

    return handled;
}
