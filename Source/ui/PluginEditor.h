// DrumSpread - plugin editor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Displays.h"
#include "LookAndFeel.h"
#include "ModulePanel.h"

class DrumSpreadProcessor;

class DrumSpreadEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit DrumSpreadEditor (DrumSpreadProcessor&);
    ~DrumSpreadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Current visual order of the four reorderable panels (slot types) - tests. */
    std::array<int, 4> displayedOrder() const noexcept { return shownOrderArr; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutChain();
    void setOrder (const std::array<int, 4>& order);
    void showPresetMenu();
    void savePreset();
    void updatePresetLabel();

    DrumSpreadProcessor& proc;
    dsui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    // top bar
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn, saveBtn { "SAVE" };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "COPY" };
    std::unique_ptr<dsui::ParamCombo> osCombo;
    std::unique_ptr<dsui::Knob> intensity;
    std::unique_ptr<dsui::ParamToggle> bypassBtn;
    juce::Rectangle<int> topBar, logoArea, osLabelArea;

    // chain
    std::array<std::unique_ptr<dsui::ModulePanel>, 6> panels; // indexed by module id
    std::array<juce::Rectangle<int>, 4> slotRects;
    juce::Rectangle<int> chainArea;
    std::array<int, 4> shownOrderArr { 0, 2, 3, 1 };
    int shownOrderIndex = -1;
    dsui::ModulePanel* dragPanel = nullptr;
    juce::Rectangle<int> dragOrigin;

    // master
    std::vector<std::unique_ptr<dsui::Knob>> masterKnobs;
    std::unique_ptr<dsui::ParamToggle> clipBtn;
    dsui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    dsui::SpectrumScope scope;
    std::vector<float> scopeBuf;
    juce::Rectangle<int> masterArea, masterKnobArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumSpreadEditor)
};
