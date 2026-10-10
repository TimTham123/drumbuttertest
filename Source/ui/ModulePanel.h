// DrumSpread - one module panel (header, module controls, shared focus/target/mix strip).
#pragma once

#include "Displays.h"
#include "Params.h"

class DrumSpreadProcessor;

namespace dsui
{
class ModulePanel : public juce::Component
{
public:
    ModulePanel (DrumSpreadProcessor& p, int moduleId);

    int moduleId() const noexcept { return module; }
    bool isReorderable() const noexcept { return module >= ds::kModWarp && module <= ds::kModHeat; }

    /** Called ~30 Hz by the editor: LEDs, GR meter, toggles, active state. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (ModulePanel&)> onDragStart;
    std::function<void (ModulePanel&, int dx)> onDragMove;
    std::function<void (ModulePanel&)> onDragEnd;
    std::function<void (ModulePanel&, int direction)> onMove;

    juce::Rectangle<int> headerArea() const;

private:
    struct Cell
    {
        juce::Component* comp = nullptr;
        float weight = 1.0f;
    };
    using Row = std::vector<Cell>;

    Knob* addKnob (int param, const juce::String& label = {});
    ParamToggle* addToggle (int param, const juce::String& text, bool invert = false);
    void layoutRows (juce::Rectangle<int> area, const std::vector<Row>& rows, const std::vector<float>& heights);

    DrumSpreadProcessor& proc;
    int module;
    juce::Colour accent;
    juce::String title;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<ParamToggle>> toggles;
    std::unique_ptr<ParamCombo> modeCombo, noiseCombo;
    std::unique_ptr<ParamToggle> power;
    juce::TextButton leftBtn { "<" }, rightBtn { ">" };
    std::unique_ptr<TriggerLed> subLed, noiseLed;
    std::unique_ptr<GrMeter> gr;
    std::vector<Row> rows, sharedRow;
    std::vector<float> rowHeights;
    juce::Label subLabel, noiseLabel;
    bool dragging = false;
    int dragStartX = 0;
};

} // namespace dsui
