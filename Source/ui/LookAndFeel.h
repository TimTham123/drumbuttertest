// DrumSpread - original visual design language ("ink & signal").
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dsui
{
namespace Colours
{
    const juce::Colour bg { 0xff12151c };
    const juce::Colour panel { 0xff1b202b };
    const juce::Colour panelHi { 0xff232937 };
    const juce::Colour well { 0xff151922 };
    const juce::Colour track { 0xff2c3342 };
    const juce::Colour text { 0xffe6e9ef };
    const juce::Colour textDim { 0xff8a93a6 };
    const juce::Colour layer { 0xfff5b041 };
    const juce::Colour warp { 0xffa78bfa };
    const juce::Colour room { 0xff2dd4bf };
    const juce::Colour press { 0xfffb7185 };
    const juce::Colour heat { 0xfffb923c };
    const juce::Colour smash { 0xffa3e635 };
    const juce::Colour master { 0xff7dd3fc };

    juce::Colour forModule (int module);
} // namespace Colours

/** Font helper (JUCE 8 FontOptions). */
juce::Font font (float height, bool bold = false);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

} // namespace dsui
