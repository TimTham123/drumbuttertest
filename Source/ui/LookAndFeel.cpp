#include "LookAndFeel.h"

#include "Params.h"

namespace dsui
{
juce::Colour Colours::forModule (int module)
{
    switch (module)
    {
        case ds::kModLayer: return layer;
        case ds::kModWarp: return warp;
        case ds::kModRoom: return room;
        case ds::kModPress: return press;
        case ds::kModHeat: return heat;
        case ds::kModSmash: return smash;
        default: return master;
    }
}

juce::Font font (float height, bool bold)
{
    auto opts = juce::FontOptions (height);
    if (bold)
        opts = opts.withStyle ("Bold");
    return juce::Font (opts);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panelHi);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::headerTextColourId, Colours::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::master.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::backgroundColourId, Colours::well);
    setColour (juce::ComboBox::outlineColourId, Colours::track);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::TextButton::buttonColourId, Colours::well);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::bg);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Colours::well);
    setColour (juce::Slider::textBoxOutlineColourId, Colours::track);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelHi);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::well);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::track);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    const auto accent = s.getProperties().contains ("accent")
                            ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["accent"])
                            : Colours::master;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float radius = size * 0.5f;
    const float thick = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - thick * 0.5f;

    // disc
    g.setColour (Colours::well);
    g.fillEllipse (c.x - radius + thick, c.y - radius + thick, (radius - thick) * 2.0f, (radius - thick) * 2.0f);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colours::track);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar parameters draw from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = start + zeroPos * (end - start);
    const float a1 = start + pos * (end - start);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    const bool enabled = s.isEnabled();
    g.setColour (enabled ? accent : accent.withSaturation (0.1f).withAlpha (0.5f));
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer dot
    const float pr = arcR - thick * 1.4f;
    const juce::Point<float> p (c.x + pr * std::sin (a1), c.y - pr * std::cos (a1));
    g.setColour (Colours::text.withAlpha (enabled ? 0.9f : 0.4f));
    g.fillEllipse (p.x - thick * 0.35f, p.y - thick * 0.35f, thick * 0.7f, thick * 0.7f);

    // value text
    if (radius > 14.0f)
    {
        g.setColour (Colours::text.withAlpha (enabled ? 1.0f : 0.5f));
        g.setFont (font (juce::jlimit (8.5f, 19.0f, radius * 0.34f)));
        auto txt = s.getTextFromValue (s.getValue());
        g.drawFittedText (txt, juce::Rectangle<int> ((int) (c.x - radius * 0.7f), (int) (c.y - radius * 0.3f),
                                                     (int) (radius * 1.4f), (int) (radius * 0.6f)),
                          juce::Justification::centred, 1, 0.7f);
    }
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto accent = b.getProperties().contains ("accent")
                            ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["accent"])
                            : Colours::master;
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const float rad = r.getHeight() * 0.5f;
    const bool on = b.getToggleState();
    g.setColour (on ? accent : Colours::well);
    g.fillRoundedRectangle (r, rad);
    g.setColour (on ? accent.brighter (0.2f) : (highlighted ? Colours::textDim : Colours::track));
    g.drawRoundedRectangle (r, rad, 1.2f);
    g.setColour (on ? Colours::bg : Colours::text.withAlpha (0.85f));
    g.setFont (font (juce::jlimit (9.0f, 14.0f, r.getHeight() * 0.55f), true));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto accent = b.getProperties().contains ("accent")
                            ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["accent"])
                            : Colours::master;
    g.setColour (on ? accent : (down ? Colours::panelHi.brighter (0.1f) : Colours::well));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? Colours::textDim : Colours::track);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? Colours::bg : Colours::text);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 1), juce::Justification::centred, 1, 0.7f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jlimit (9.0f, 15.0f, h * 0.48f)); }

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Colours::textDim : Colours::track);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const float ah = (float) h * 0.18f;
    const float ax = (float) w - (float) h * 0.55f, ay = (float) h * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - ah, ay - ah * 0.5f, ax + ah, ay - ah * 0.5f, ax, ay + ah * 0.7f);
    g.setColour (Colours::textDim);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& b) { return font (juce::jlimit (9.0f, 15.0f, b.getHeight() * 0.5f)); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - box.getHeight(), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (15.0f); }

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.0f));
    return l;
}

} // namespace dsui
