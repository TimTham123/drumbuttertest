#include "ModulePanel.h"

#include "PluginProcessor.h"

namespace dsui
{
using namespace ds;

ModulePanel::ModulePanel (DrumSpreadProcessor& p, int m) : proc (p), module (m)
{
    accent = Colours::forModule (m);
    title = moduleName (m);

    power = std::make_unique<ParamToggle> (proc, sharedParam (m, kSfBypass), "ON", accent, true);
    power->setTooltip (juce::String (title) + " on/off (bypass, click-free)");
    addAndMakeVisible (*power);

    auto addMode = [this] (int param) {
        modeCombo = std::make_unique<ParamCombo> (proc, param);
        addAndMakeVisible (*modeCombo);
    };

    switch (m)
    {
        case kModLayer:
        {
            noiseCombo = std::make_unique<ParamCombo> (proc, kLayerNoiseType);
            addAndMakeVisible (*noiseCombo);
            subLed = std::make_unique<TriggerLed> (accent);
            noiseLed = std::make_unique<TriggerLed> (accent.brighter (0.4f));
            addAndMakeVisible (*subLed);
            addAndMakeVisible (*noiseLed);
            for (auto* l : { &subLabel, &noiseLabel })
            {
                l->setFont (font (13.0f, true));
                l->setColour (juce::Label::textColourId, accent);
                addAndMakeVisible (*l);
            }
            subLabel.setText ("SUB", juce::dontSendNotification);
            noiseLabel.setText ("NOISE", juce::dontSendNotification);
            rows.push_back ({ { addKnob (kLayerSubSens, "Sens") }, { addKnob (kLayerSubFreq, "Freq") }, { addKnob (kLayerSubDrop, "Drop") } });
            rows.push_back ({ { addKnob (kLayerSubDecay, "Decay") }, { addKnob (kLayerSubGrowl, "Growl") }, { addKnob (kLayerSubLevel, "Level") } });
            rows.push_back ({ { addKnob (kLayerNoiseSens, "Sens") }, { addKnob (kLayerNoiseTone, "Tone") } });
            rows.push_back ({ { addKnob (kLayerNoiseDecay, "Decay") }, { addKnob (kLayerNoiseLevel, "Level") } });
            break;
        }
        case kModWarp:
            addMode (kWarpMode);
            rows.push_back ({ { addKnob (kWarpPitch) } });
            rows.push_back ({ { addKnob (kWarpFormant) } });
            rows.push_back ({ { addToggle (kWarpLink, "LINK FORMANT") } });
            rowHeights = { 1.0f, 1.0f, 0.24f };
            break;
        case kModRoom:
            addMode (kRoomType);
            rows.push_back ({ { addKnob (kRoomSize) }, { addKnob (kRoomDecay) } });
            rows.push_back ({ { addKnob (kRoomPredelay, "Pre") }, { addKnob (kRoomTone) }, { addKnob (kRoomWidth) } });
            rowHeights = { 1.15f, 0.9f };
            break;
        case kModPress:
            addMode (kPressType);
            rows.push_back ({ { addKnob (kPressAmount) } });
            rows.push_back ({ { addKnob (kPressAttack) }, { addKnob (kPressRelease) }, { addKnob (kPressScHpf, "SC HPF") } });
            rows.push_back ({ { addToggle (kPressMakeup, "MAKEUP") }, { addToggle (kPressKnee, "SOFT") }, { addToggle (kPressAllButtons, "ALL") } });
            gr = std::make_unique<GrMeter>();
            addAndMakeVisible (*gr);
            rows.push_back ({ { gr.get() } });
            rowHeights = { 1.1f, 0.85f, 0.22f, 0.16f };
            break;
        case kModHeat:
            addMode (kHeatMode);
            rows.push_back ({ { addKnob (kHeatDrive, "Drive") } });
            rows.push_back ({ { addKnob (kHeatTone, "Tone") }, { addKnob (kHeatTrim, "Trim") } });
            rowHeights = { 1.25f, 0.85f };
            break;
        case kModSmash:
        default:
            addMode (kSmashCurve);
            rows.push_back ({ { addKnob (kSmashDrive, "Drive") }, { addKnob (kSmashBias) } });
            rows.push_back ({ { addKnob (kSmashCeiling) }, { addKnob (kSmashTrim, "Trim") } });
            rows.push_back ({ { addToggle (kSmashClipMode, "HARD CLIP") } });
            rowHeights = { 1.0f, 1.0f, 0.24f };
            break;
    }

    sharedRow.push_back ({ { addKnob (sharedParam (m, kSfFocusLo), "Lo") },
                          { addKnob (sharedParam (m, kSfFocusHi), "Hi") },
                          { addKnob (sharedParam (m, kSfTarget), "Target") },
                          { addKnob (sharedParam (m, kSfMix), "Mix") } });

    if (isReorderable())
    {
        for (auto* b : { &leftBtn, &rightBtn })
        {
            addAndMakeVisible (*b);
            b->setTooltip ("Move this module earlier/later in the chain (or drag the header)");
        }
        leftBtn.onClick = [this] { if (onMove) onMove (*this, -1); };
        rightBtn.onClick = [this] { if (onMove) onMove (*this, +1); };
    }
    if (modeCombo)
        modeCombo->setTooltip (juce::String (title) + " character / mode");
}

Knob* ModulePanel::addKnob (int param, const juce::String& label)
{
    knobs.push_back (std::make_unique<Knob> (proc, param, accent, label));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

ParamToggle* ModulePanel::addToggle (int param, const juce::String& text, bool invert)
{
    toggles.push_back (std::make_unique<ParamToggle> (proc, param, text, accent, invert));
    addAndMakeVisible (*toggles.back());
    return toggles.back().get();
}

juce::Rectangle<int> ModulePanel::headerArea() const
{
    // row 1: grip, title, power; row 2: mode selector and move buttons
    return getLocalBounds().removeFromTop (2 * juce::jlimit (22, 34, getHeight() / 15) + 4);
}

void ModulePanel::layoutRows (juce::Rectangle<int> area, const std::vector<Row>& rws, const std::vector<float>& heights)
{
    float total = 0.0f;
    for (size_t i = 0; i < rws.size(); ++i)
        total += i < heights.size() ? heights[i] : 1.0f;
    const int h0 = area.getHeight();
    for (size_t i = 0; i < rws.size(); ++i)
    {
        const float w = i < heights.size() ? heights[i] : 1.0f;
        auto rowArea = area.removeFromTop (i + 1 == rws.size() ? area.getHeight() : (int) ((float) h0 * w / total));
        float tw = 0.0f;
        for (const auto& c : rws[i])
            tw += c.weight;
        const int w0 = rowArea.getWidth();
        for (size_t k = 0; k < rws[i].size(); ++k)
        {
            auto cell = rowArea.removeFromLeft (k + 1 == rws[i].size() ? rowArea.getWidth() : (int) ((float) w0 * rws[i][k].weight / tw));
            if (auto* comp = rws[i][k].comp)
            {
                if (dynamic_cast<juce::ToggleButton*> (comp) != nullptr || dynamic_cast<GrMeter*> (comp) != nullptr)
                    comp->setBounds (cell.reduced (3, 2));
                else
                    comp->setBounds (cell.reduced (2, 1));
            }
        }
    }
}

void ModulePanel::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto header = r.removeFromTop (headerArea().getHeight());
    const int hh = (header.getHeight() - 4) / 2;
    auto row1 = header.removeFromTop (hh + 4);
    auto row2 = header;
    power->setBounds (row1.removeFromRight (juce::jmin (hh * 2, row1.getWidth() / 3)).reduced (2, 3));
    if (isReorderable())
    {
        rightBtn.setBounds (row2.removeFromRight (hh).reduced (2, 3));
        leftBtn.setBounds (row2.removeFromRight (hh).reduced (2, 3));
    }
    if (modeCombo)
        modeCombo->setBounds (row2.reduced (2, 2));

    // shared strip at the bottom
    auto shared = r.removeFromBottom (juce::jmax (48, getHeight() / 6));
    r.removeFromBottom (4);
    layoutRows (shared, sharedRow, { 1.0f });

    if (module == kModLayer)
    {
        auto left = r.removeFromLeft (r.getWidth() * 3 / 5);
        auto right = r.reduced (4, 0);
        // the panel's second header row carries the SUB / NOISE labels, LEDs and noise type
        auto lh = row2.removeFromLeft (left.getWidth());
        subLabel.setBounds (lh.removeFromLeft (hh * 2));
        subLed->setBounds (lh.removeFromLeft (hh).reduced (3));
        noiseLabel.setBounds (row2.removeFromLeft ((int) (hh * 2.2f)));
        noiseLed->setBounds (row2.removeFromLeft (hh).reduced (3));
        noiseCombo->setBounds (right.removeFromTop (hh).reduced (2, 1));
        right.removeFromTop (2);
        layoutRows (left, { rows[0], rows[1] }, { 1.0f, 1.0f });
        layoutRows (right, { rows[2], rows[3] }, { 1.0f, 1.0f });
    }
    else
        layoutRows (r, rows, rowHeights);
}

void ModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 9.0f);
    // accent rail along the top
    auto header = headerArea().toFloat().reduced (6.0f, 0.0f).withTrimmedTop (4.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (r.getX() + 10.0f, r.getY() + 1.5f, r.getWidth() - 20.0f, 2.5f, 1.25f);
    // grip + title (first header row)
    header = header.withHeight ((header.getHeight() + 4.0f) * 0.5f);
    float x = header.getX();
    if (isReorderable())
    {
        g.setColour (dragging ? accent : Colours::textDim);
        const float gy = header.getCentreY();
        for (int row = -1; row <= 1; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (x + 2.0f + (float) col * 5.0f, gy + (float) row * 5.0f - 1.5f, 3.0f, 3.0f);
        x += header.getHeight() * 0.5f;
    }
    g.setColour (Colours::text);
    g.setFont (font (juce::jlimit (12.0f, 19.0f, header.getHeight() * 0.62f), true));
    g.drawText (title, juce::Rectangle<float> (x + 4.0f, header.getY(), header.getWidth() * 0.6f, header.getHeight()).toNearestInt(),
                juce::Justification::centredLeft);
    // divider above the shared strip
    const float sy = (float) getHeight() - (float) juce::jmax (48, getHeight() / 6) - 7.0f;
    g.setColour (Colours::track);
    for (float dx = 10.0f; dx < r.getWidth() - 10.0f; dx += 6.0f)
        g.fillRect (dx, sy, 3.0f, 1.0f);
}

void ModulePanel::refresh()
{
    power->sync();
    for (auto& t : toggles)
        t->sync();
    auto& tele = proc.telemetry();
    if (subLed)
        subLed->update (tele.subTriggers.load());
    if (noiseLed)
        noiseLed->update (tele.noiseTriggers.load());
    if (gr)
        gr->setGr (tele.pressGrDb.load());
    const bool active = proc.getParameterValue (sharedParam (module, kSfBypass)) < 0.5f;
    for (auto& k : knobs)
        k->setAlpha (active ? 1.0f : 0.45f);
}

void ModulePanel::mouseDown (const juce::MouseEvent& e)
{
    if (isReorderable() && headerArea().contains (e.getPosition()))
    {
        dragging = true;
        dragStartX = e.getScreenX();
        if (onDragStart)
            onDragStart (*this);
        repaint();
    }
}

void ModulePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && onDragMove)
        onDragMove (*this, e.getScreenX() - dragStartX);
}

void ModulePanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        if (onDragEnd)
            onDragEnd (*this);
        repaint();
    }
}

} // namespace dsui
